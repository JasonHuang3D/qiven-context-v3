// qiven3-child: the processes the tests start by subprocess, one binary,
// its first argument the subcommand. A subcommand is one row of the table
// below: its name, its function (child/child.hpp) and its usage line. An
// unknown or missing subcommand prints the usage lines on standard error
// and ends with child::usage_status. On Windows its entry is wmain, as the
// product's: each argument converted from UTF-16 by the Windows adapter
// (run_utf8), one that is not valid UTF-16 ending it with usage_status.
// run is noexcept, as run_utf8's entry is: an exception ends the child
// there, by one line on standard error and child::exception_status.

#include <array>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "child/child.hpp"

#if defined(_WIN32)
#include "platform/win/windows.hpp"
#endif

namespace {

// The table's row. run is a plain function, picked by the name argv gives
// at run time.
struct Subcommand {
    std::string_view name;
    int (*run)(std::span<const std::string_view> args);
    std::string_view usage;
};

constexpr std::array subcommands{
    Subcommand{"probe", qiven3::child::probe, "probe match|parse|refs: one facility over each input line (probe.cpp)"},
    Subcommand{"platform", qiven3::child::platform,
               "platform sleep|env|argv|flood|print|cat|replace|tree|gone|init ...: "
               "the process cases' children (platform.cpp)"},
};

int usage(std::string_view why) {
    std::cerr << "qiven3-child: " << why << "\n";
    for (const Subcommand& s : subcommands) {
        std::cerr << "  qiven3-child " << s.usage << "\n";
    }
    return qiven3::child::usage_status;
}

int run(int argc, const char* const* argv) noexcept {
    try {
        const std::span<const char* const> given(argv, argc > 0 ? static_cast<std::size_t>(argc) : 0U);
        std::vector<std::string_view> args;
        for (std::size_t i = 1; i < given.size(); ++i) {
            args.emplace_back(given[i]);
        }
        if (args.empty()) {
            return usage("no subcommand");
        }
        for (const Subcommand& s : subcommands) {
            if (s.name == args.front()) {
                return s.run(std::span<const std::string_view>(args).subspan(1));
            }
        }
        return usage("an unknown subcommand");
    } catch (...) {
        std::fputs("qiven3-child: an exception ended the subcommand\n", stderr);
        return qiven3::child::exception_status;
    }
}

}  // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    return qiven3::platform::win::run_utf8(argc, argv, &run, qiven3::child::usage_status);
}
#else
int main(int argc, char** argv) { return run(argc, argv); }
#endif
