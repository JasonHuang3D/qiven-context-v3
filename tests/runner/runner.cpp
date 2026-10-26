#include "runner/runner.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <set>

#include "log/catalogue.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "support/log/journal.hpp"

namespace qiven3::test {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(log::Name name, CaseFn fn, bool serial) {
    registry().push_back(Case{name, fn, serial});
}

memory::Arena& Counted::arena(std::size_t bytes) {
    Owned<memory::Arena>& owned = arenas_.emplace_back(bytes);
    return owned.allocator.emplace(owned.span());
}

memory::Pool& Counted::pool(std::size_t slot, std::size_t bytes) {
    Owned<memory::Pool>& owned = pools_.emplace_back(bytes);
    return owned.allocator.emplace(slot, owned.span());
}

memory::Heap& Counted::heap(std::size_t bytes) {
    Owned<memory::Heap>& owned = heaps_.emplace_back(bytes);
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
    const auto each = [&](const auto& list) {
        for (const auto& owned : list) {
            sum = memory::add(sum, owned.allocator->used()).value_or(SIZE_MAX);
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

namespace {

// The outcomes' names, made once: a Name made within a conditional
// expression is not made right on every compiler.
constexpr log::Name passed = "pass";
constexpr log::Name skipped = "skip";
constexpr log::Name failed = "fail";

// The case's end in log: its outcome's name, the time since start and the
// bytes ctx's allocators hold; status, given back.
int ended(const Case& c, const Context& ctx, std::chrono::steady_clock::time_point start, int status) {
    const log::Name outcome = status == exit_pass ? passed : status == exit_skip ? skipped : failed;
    log::event(ctx.log, log::events::case_end, c.name, outcome, log::Millis::since(start),
               log::Count(ctx.counted.held()));
    return status;
}

}  // namespace

int run_case(const Case& c, std::ostream& out, log::Log& log) {
    const auto start = std::chrono::steady_clock::now();
    Context ctx{out, log};
    try {
        c.fn(ctx);
    } catch (const Skip& skip) {
        out << "SKIP " << c.name.text() << ": " << skip.reason << "\n";
        out.flush();
        return ended(c, ctx, start, exit_skip);
    } catch (const Stop&) {
        out << "stopped by a failed REQUIRE\n";
    } catch (const std::exception& e) {
        ++ctx.failures;
        out << "exception: " << e.what() << "\n";
    } catch (const platform::PlatformError& e) {
        // A plain value with no base: its code's name and its detail.
        ++ctx.failures;
        out << "platform error " << platform::name(e.code) << ": " << e.detail.view() << "\n";
    } catch (...) {
        ++ctx.failures;
        out << "exception of an unknown type\n";
    }
    ctx.failures += ctx.counted.holding(out);
    if (ctx.failures == 0) {
        out << "PASS " << c.name.text() << "\n";
        out.flush();
        return ended(c, ctx, start, exit_pass);
    }
    out << "FAIL " << c.name.text() << ": " << ctx.failures << " failed\n";
    out.flush();
    return ended(c, ctx, start, exit_fail);
}

namespace {

// An empty or repeated name, written to err; the registry is then unusable.
bool names_valid(const std::vector<Case>& cases, std::ostream& err) {
    std::set<std::string_view> seen;
    bool ok = true;
    for (const Case& c : cases) {
        const std::string_view name = c.name.text();
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
             std::ostream& out, std::ostream& err, log::Log& log) {
    if (!names_valid(cases, err)) {
        return exit_usage;
    }
    if (args.size() == 1 && args[0] == "--list") {
        std::vector<const Case*> sorted;
        for (const Case& c : cases) {
            sorted.push_back(&c);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const Case* a, const Case* b) { return a->name.text() < b->name.text(); });
        for (const Case* c : sorted) {
            out << c->name.text() << (c->serial ? "\tserial\n" : "\n");
        }
        out.flush();
        return exit_pass;
    }
    if (args.size() == 2 && args[0] == "--run") {
        for (const Case& c : cases) {
            if (c.name.text() == args[1]) {
                return run_case(c, out, log);
            }
        }
        err << "no such case: " << args[1] << "\n";
        return exit_usage;
    }
    return usage(err);
}

}  // namespace qiven3::test

namespace {

// The process's log's pages, of static storage.
qiven3::test::Journal::Pages journal_pages;

}  // namespace

int main(int argc, char** argv) {
    // A crash, an abort or a failed assertion is the diagnostic entry's,
    // which every binary runs before main: a report on standard error, the
    // process's status, and no dialog.
    // Unbuffered, so what a case wrote before a crash is in the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // The process's log, its ring in a crash's report, drained at its end.
    qiven3::test::Journal journal(journal_pages, "runner");
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return qiven3::test::run_main(args, qiven3::test::registry(), std::cout, std::cerr, journal.log());
}
