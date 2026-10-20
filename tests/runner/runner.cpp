#include "runner/runner.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <set>

#ifdef _WIN32
#include <crtdbg.h>
#include <stdlib.h>
#include <windows.h>
#endif

namespace qiven3::test {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(const char* group, const char* name, CaseFn fn, bool serial) {
    registry().push_back(Case{std::string(group) + "." + name, fn, serial});
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

bool check(Context& ctx, bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        ++ctx.failures;
        ctx.out << file << "(" << line << "): CHECK failed: " << expr << "\n";
        ctx.out.flush();
    }
    return ok;
}

int run_case(const Case& c, std::ostream& out) {
    Context ctx{out};
    try {
        c.fn(ctx);
    } catch (const Skip& skip) {
        out << "SKIP " << c.name << ": " << skip.reason << "\n";
        out.flush();
        return exit_skip;
    } catch (const Stop&) {
        out << "stopped by a failed REQUIRE\n";
    } catch (const std::exception& e) {
        ++ctx.failures;
        out << "exception: " << e.what() << "\n";
    } catch (...) {
        ++ctx.failures;
        out << "exception of an unknown type\n";
    }
    ctx.failures += ctx.counted.holding(out);
    if (ctx.failures == 0) {
        out << "PASS " << c.name << "\n";
        out.flush();
        return exit_pass;
    }
    out << "FAIL " << c.name << ": " << ctx.failures << " failed\n";
    out.flush();
    return exit_fail;
}

namespace {

// An empty or repeated name, written to err; the registry is then unusable.
bool names_valid(const std::vector<Case>& cases, std::ostream& err) {
    std::set<std::string> seen;
    bool ok = true;
    for (const Case& c : cases) {
        if (c.name.empty() || c.name.front() == '.' || c.name.back() == '.') {
            err << "a case has an incomplete name: '" << c.name << "'\n";
            ok = false;
        } else if (!seen.insert(c.name).second) {
            err << "two cases are named " << c.name << "\n";
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
             std::ostream& out, std::ostream& err) {
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
            out << c->name << (c->serial ? "\tserial\n" : "\n");
        }
        out.flush();
        return exit_pass;
    }
    if (args.size() == 2 && args[0] == "--run") {
        for (const Case& c : cases) {
            if (c.name == args[1]) {
                return run_case(c, out);
            }
        }
        err << "no such case: " << args[1] << "\n";
        return exit_usage;
    }
    return usage(err);
}

}  // namespace qiven3::test

int main(int argc, char** argv) {
#ifdef _WIN32
    // A crash or an abort ends the process with its status, and no dialog
    // waits for a person.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
#else
    // fail_fast's std::terminate ends the process by abort's signal, without
    // the line the C++ runtime's own terminate handler writes first.
    std::set_terminate([] { std::abort(); });
#endif
    // Unbuffered, so what a case wrote before a crash is in the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return qiven3::test::run_main(args, qiven3::test::registry(), std::cout, std::cerr);
}
