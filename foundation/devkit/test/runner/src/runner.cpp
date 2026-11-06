#include "qiven/runner/core.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <set>
#include <string_view>

#include "qiven/memory/memory.hpp"

namespace qiven::runner {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(std::string_view name, const qiven::log::Name* logged, CaseFn fn) noexcept {
    try {
        registry().push_back(Case{name, logged, fn});
    } catch (...) {
        qiven::memory::fail_fast("runner::Registrar: the registry could not take a case");
    }
}

namespace {

std::vector<Namer>& namers() {
    static std::vector<Namer> registered;
    return registered;
}

// The error in flight named by the first namer that knows it; false if none.
bool named(std::ostream& out) {
    for (const Namer namer : namers()) {
        if (namer(out)) {
            return true;
        }
    }
    return false;
}

// An empty list of Counted's, made in place in the caller's member; an
// allocation failure of its own ends by the fail-fast.
template <class List>
List empty_list() noexcept {
    try {
        return List();
    } catch (...) {
        qiven::memory::fail_fast("runner::Counted: a list of its allocators could not be made");
    }
}

}  // namespace

NamerRegistrar::NamerRegistrar(Namer namer) noexcept {
    try {
        if (std::find(namers().begin(), namers().end(), namer) == namers().end()) {
            namers().push_back(namer);
        }
    } catch (...) {
        qiven::memory::fail_fast("runner::NamerRegistrar: the namers could not take a namer");
    }
}

Counted::Counted() noexcept
    : arenas_(empty_list<decltype(arenas_)>()), pools_(empty_list<decltype(pools_)>()),
      heaps_(empty_list<decltype(heaps_)>()) {}

qiven::memory::Arena& Counted::arena(std::size_t bytes) {
    Owned<qiven::memory::Arena>& owned = arenas_.emplace_back(bytes);
    return owned.allocator.emplace(owned.span());
}

qiven::memory::Pool& Counted::pool(std::size_t slot, std::size_t bytes) {
    Owned<qiven::memory::Pool>& owned = pools_.emplace_back(bytes);
    return owned.allocator.emplace(slot, owned.span());
}

qiven::memory::Heap& Counted::heap(std::size_t bytes) {
    Owned<qiven::memory::Heap>& owned = heaps_.emplace_back(bytes);
    return owned.allocator.emplace(owned.span());
}

int Counted::holding(std::ostream& out) const {
    int found = 0;
    const auto each = [&](const char* kind, const auto& list) {
        std::size_t index = 0;
        for (const auto& owned : list) {
            ++index;
            const std::size_t used = owned.allocator->used();
            if (used != 0) {
                ++found;
                out << "held at the case's end: " << kind << " " << index << ", " << used << " bytes\n";
            }
        }
    };
    each("arena", arenas_);
    each("pool", pools_);
    each("heap", heaps_);
    out.flush();
    return found;
}

std::size_t Counted::held() const noexcept {
    std::size_t sum = 0;
    const auto each = [&](const auto& list) noexcept {
        for (const auto& owned : list) {
            sum = qiven::memory::add(sum, owned.allocator->used()).value_or(SIZE_MAX);
        }
    };
    each(arenas_);
    each(pools_);
    each(heaps_);
    return sum;
}

bool check(Context& ctx, bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        ++ctx.failures;
        ctx.out << file << "(" << line << "): CHECK failed: " << expr << "\n";
        ctx.out.flush();
    }
    return ok;
}

int run_case(const Case& c, std::ostream& out, qiven::log::Log& log, Ended ended_by) {
    const auto start = std::chrono::steady_clock::now();
    Context ctx{out, log};
    // The case's end given to ended_by; status, given back.
    const auto ended = [&](int status) {
        if (ended_by != nullptr) {
            ended_by(log, c, status, start, ctx.counted.held());
        }
        return status;
    };
    try {
        c.fn(ctx);
    } catch (const Skip& skip) {
        out << "SKIP " << c.name << ": " << skip.reason << "\n";
        out.flush();
        return ended(exit_skip);
    } catch (const Stop&) {
        out << "stopped by a failed REQUIRE\n";
    } catch (const std::exception& e) {
        ++ctx.failures;
        out << "exception: " << e.what() << "\n";
    } catch (const qiven::memory::MemoryError& e) {
        // A plain value with no base: its code's name, the operation that
        // raised it (the allocator) and its detail.
        ++ctx.failures;
        out << "memory error " << qiven::memory::name(e.code) << " in " << e.operation.view() << ": " << e.detail.view()
            << "\n";
    } catch (...) {
        // Another module's error (a PlatformError, a StoreError) by the
        // namer its cases registered.
        ++ctx.failures;
        if (!named(out)) {
            out << "exception of an unknown type\n";
        }
    }
    ctx.failures += ctx.counted.holding(out);
    if (ctx.failures == 0) {
        out << "PASS " << c.name << "\n";
        out.flush();
        return ended(exit_pass);
    }
    out << "FAIL " << c.name << ": " << ctx.failures << " failed\n";
    out.flush();
    return ended(exit_fail);
}

namespace {

// An empty or repeated name, written to err; the registry is then unusable.
bool names_valid(const std::vector<Case>& cases, std::ostream& err) {
    std::set<std::string_view> seen;
    bool ok = true;
    for (const Case& c : cases) {
        const std::string_view name = c.name;
        if (name.front() == '.' || name.back() == '.') {
            err << "a case has an incomplete name: '" << name << "'\n";
            ok = false;
        } else if (!seen.insert(name).second) {
            err << "two cases are named " << name << "\n";
            ok = false;
        }
    }
    return ok;
}

int usage(std::ostream& err) {
    err << "usage: --list | --run NAME\n";
    return exit_usage;
}

}  // namespace

int run_main(std::span<const std::string_view> args, const std::vector<Case>& cases,
             std::ostream& out, std::ostream& err, qiven::log::Log& log, Ended ended) {
    if (!names_valid(cases, err)) {
        return exit_usage;
    }
    if (args.size() == 1 && args[0] == "--list") {
        std::vector<const Case*> sorted;
        for (const Case& c : cases) {
            sorted.push_back(&c);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const Case* a, const Case* b) { return a->name < b->name; });
        for (const Case* c : sorted) {
            out << c->name << "\n";
        }
        out.flush();
        return exit_pass;
    }
    if (args.size() == 2 && args[0] == "--run") {
        for (const Case& c : cases) {
            if (c.name == args[1]) {
                return run_case(c, out, log, ended);
            }
        }
        err << "no such case: " << args[1] << "\n";
        return exit_usage;
    }
    return usage(err);
}

}  // namespace qiven::runner
