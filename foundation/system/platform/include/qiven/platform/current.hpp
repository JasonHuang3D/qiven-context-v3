#pragma once

// The adapter's choice, for a binary's main alone: a caller's other code
// never includes this header (it takes a P& passed in). Current is the
// build's one adapter type, the Windows adapter under _WIN32; where no
// adapter is built there is no Current, and adapter_for and current throw
// for every system. None is a default, nor has the caller's leaf or bound
// one.

#include <cstddef>
#include <optional>
#include <string_view>

#include "qiven/platform/platform.hpp"

#if defined(_WIN32)
#include "qiven/platform/windows/windows.hpp"
#endif

namespace qiven::platform {

// The system this binary was built for: "windows", "linux", "macos" or
// "unknown".
std::string_view build_system() noexcept;

#if defined(_WIN32)
using Current = win::WindowsPlatform;

// The adapter for system, its configuration location config_dir when
// given, else the system's own folder and then the caller's leaf (the
// Windows adapter's: LocalAppData, '\', leaf), stage's bound open_bound
// (qiven/platform/files.hpp): any other system is PlatformError
// unsupported, naming it.
Current adapter_for(std::string_view system, std::string_view leaf, std::size_t open_bound,
                    std::optional<std::string_view> config_dir);

// adapter_for(build_system(), leaf, open_bound, config_dir).
Current current(std::string_view leaf, std::size_t open_bound, std::optional<std::string_view> config_dir);
#else
[[noreturn]] void adapter_for(std::string_view system, std::string_view leaf, std::size_t open_bound,
                              std::optional<std::string_view> config_dir);
[[noreturn]] void current(std::string_view leaf, std::size_t open_bound, std::optional<std::string_view> config_dir);
#endif

}  // namespace qiven::platform
