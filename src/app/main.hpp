#pragma once

// main: the qiven3 binary's subcommands, its adapter and its exit status.
// `qiven3 location ARGUMENTS`, the store location's commands, takes the
// arguments after its name. main reads the command line through the
// per-call wire (wire/wire.hpp) within the binary's bounds: a request the
// wire cannot read, an argument not UTF-8 or past a bound (more than
// max_arguments arguments among them), exits 2 with no line, the wire's
// own failure. It then makes the platform by the maker its binary gives
// it, current(), with the subcommand's --config, and runs the subcommand
// on it, whose lines go through the wire and whose exit status is the
// binary's. A usage error of the binary's own, no subcommand or another,
// answers usage_line() and exits 2. Where the platform cannot be made (no
// adapter is built for the system, or the adapter refuses), nothing can
// print the error: core writes only through a platform, so main exits 1
// with no line, or 2 for a command line that is a usage error.
//
// The command line's text is UTF-8: on Windows the binary's wmain converts
// the system's UTF-16 arguments to it (app/instances.cpp), an argument it
// cannot convert exiting 2 with no line; elsewhere the wire refuses an
// argument that is not.
//
// main is a template on the maker; app/main_impl.hpp defines it, and the
// binary's instantiation file, app/instances.cpp, alone includes it.
//
// Budget: the arguments' views on the stack, max_arguments of them, and
// nothing allocated here; the subcommand's own budget beyond. Platform
// calls: the maker's, then the subcommand's.
//
// thread: one, the process's first.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "wire/wire.hpp"

namespace qiven3 {
namespace app {

// The binary's own exit statuses; a subcommand's are its own.
inline constexpr int refused = 1;
inline constexpr int usage = 2;

// The most arguments a command line holds after the binary's name, past
// every subcommand's longest form: more is past the wire's bound.
inline constexpr std::size_t max_arguments = 16;

// The most bytes of one argument and of all: a Windows command line's
// 32,767 UTF-16 units at 3 bytes each, so that no command line the system
// gives is past them by its bytes.
inline constexpr std::size_t max_argument_bytes = std::size_t{3} * 32767;

// The wire's bounds on the binary's request.
inline constexpr wire::Bounds bounds{max_arguments, max_argument_bytes, max_argument_bytes};

enum class Subcommand : std::uint8_t { location };

// The arguments' views, the caller's.
using Views = std::array<std::string_view, max_arguments>;

// A command line read: the subcommand and its own arguments, from its
// first on, views into the caller's Views.
struct CommandView {
    std::span<const std::string_view> arguments;
    Subcommand subcommand = Subcommand::location;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(CommandView) == 24, "CommandView's size is pinned: a span and the subcommand");
#endif

// The request's arguments into views, read: none for a usage error, no
// subcommand or another, or more than max_arguments.
[[nodiscard]] std::optional<CommandView> command(const wire::RequestView& request, Views& views) noexcept;

// Whether the subcommand's own arguments are well formed.
[[nodiscard]] bool well_formed(const CommandView& command) noexcept;

// The configuration override the platform is made with: the subcommand's
// --config, none where it names none.
[[nodiscard]] std::optional<std::string_view> config_option(const CommandView& command) noexcept;

// The binary's usage line.
std::string_view usage_line() noexcept;

// The command argv names, run on the platform make gives for its
// --config: the subcommand's exit status. make is the binary's current();
// where it returns no platform (no adapter is built), it throws, and main
// exits 1, or 2 for a usage error.
template <typename Make>
int main(int argc, const char* const* argv, Make make) noexcept;

}  // namespace app
}  // namespace qiven3
