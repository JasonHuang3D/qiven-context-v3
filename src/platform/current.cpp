#include "platform/current.hpp"

#if defined(_WIN32)
#include "platform/win/windows.hpp"
#endif

namespace qiven3::platform {

std::string_view build_system() noexcept {
#if defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

std::unique_ptr<Platform> adapter_for(std::string_view system,
                                      const std::optional<std::string>& config_dir) {
#if defined(_WIN32)
    if (system == "windows") {
        return std::make_unique<win::WindowsPlatform>(config_dir);
    }
#else
    (void)config_dir;
#endif
    throw PlatformError(Code::unsupported, "current",
                        "no adapter for the system '" + std::string(system) + "' is built here");
}

std::unique_ptr<Platform> current(const std::optional<std::string>& config_dir) {
    return adapter_for(build_system(), config_dir);
}

}  // namespace qiven3::platform
