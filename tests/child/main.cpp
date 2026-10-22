// qiven3-child: the processes the tests start by subprocess, one binary,
// its first argument the subcommand. A subcommand is one row of the table
// below: its name, its function (child/child.hpp) and its usage line. An
// unknown or missing subcommand prints the usage lines on standard error
// and ends with child::usage_status.

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "child/child.hpp"

namespace {

// The table's row. run is a plain function, picked by the name argv gives
// at run time.
struct Subcommand {
    std::string_view name;
    int (*run)(std::span<const std::string_view> args);
    std::string_view usage;
};

constexpr std::array subcommands{
    Subcommand{"probe", qiven3::child::probe, "probe match|parse: one facility over each input line (probe.cpp)"},
};

int usage(std::string_view why) {
    std::cerr << "qiven3-child: " << why << "\n";
    for (const Subcommand& s : subcommands) {
        std::cerr << "  qiven3-child " << s.usage << "\n";
    }
    return qiven3::child::usage_status;
}

}  // namespace

int main(int argc, char** argv) {
    const std::span<char*> given(argv, argc > 0 ? static_cast<std::size_t>(argc) : 0U);
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
}
