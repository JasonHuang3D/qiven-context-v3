#pragma once

// qiven3-child's subcommands: each a function of its arguments after its
// name, which returns the process's exit status; main.cpp's table names
// each. A subcommand reads its standard input and writes its standard
// output as its own comment states.
//
// thread: one (each subcommand runs in the process's first thread)

#include <span>
#include <string_view>

namespace qiven3::child {

// The exit status of a caller's mistake: an unknown subcommand, a wrong
// argument, a malformed input line.
inline constexpr int usage_status = 2;

// probe: applies one facility to each input line (probe.cpp).
int probe(std::span<const std::string_view> args);

// platform: the process cases' children, on Windows (platform.cpp).
int platform(std::span<const std::string_view> args);

}  // namespace qiven3::child
