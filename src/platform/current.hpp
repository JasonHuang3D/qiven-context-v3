#pragma once

// The adapter's choice, for a binary's main alone: core never includes this
// header (it takes a Platform& passed in).

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "platform/platform.hpp"

namespace qiven3::platform {

// The system this binary was built for: "windows", "linux", "macos" or
// "unknown".
std::string_view build_system() noexcept;

// The adapter for system, its configuration location overridden by
// config_dir when given. Exactly one adapter matches a system, and none is
// a default: any other system, or one whose adapter is not built here, is
// PlatformError unsupported, naming it.
std::unique_ptr<Platform> adapter_for(std::string_view system,
                                      const std::optional<std::string>& config_dir);

// adapter_for(build_system(), config_dir): the Windows adapter under _WIN32,
// unsupported elsewhere.
std::unique_ptr<Platform> current(const std::optional<std::string>& config_dir);

}  // namespace qiven3::platform
