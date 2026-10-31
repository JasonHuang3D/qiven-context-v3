#pragma once

// The adapter's choice, for a binary's main alone: core never includes this
// header (it takes a P& passed in). Current is the build's one adapter
// type, the Windows adapter under _WIN32; where no adapter is built there
// is no Current, and adapter_for and current throw for every system. None
// is a default.

#include <optional>
#include <string_view>

#include "platform/platform.hpp"

#if defined(_WIN32)
#include "platform/win/windows.hpp"
#endif

namespace qiven3::platform {

// The system this binary was built for: "windows", "linux", "macos" or
// "unknown".
std::string_view build_system() noexcept;

#if defined(_WIN32)
using Current = win::WindowsPlatform;

// The adapter for system, its configuration location overridden by
// config_dir when given: any other system is PlatformError unsupported,
// naming it.
Current adapter_for(std::string_view system, std::optional<std::string_view> config_dir);

// adapter_for(build_system(), config_dir).
Current current(std::optional<std::string_view> config_dir);
#else
[[noreturn]] void adapter_for(std::string_view system, std::optional<std::string_view> config_dir);
[[noreturn]] void current(std::optional<std::string_view> config_dir);
#endif

}  // namespace qiven3::platform
