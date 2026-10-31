#include "platform/current.hpp"

namespace qiven3::platform {
namespace {

[[noreturn]] void unsupported(std::string_view system) {
    throw PlatformError(Code::unsupported, "current",
                        Detail("no adapter for the system '").add(system).add("' is built here"));
}

}  // namespace

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

#if defined(_WIN32)
Current adapter_for(std::string_view system, std::optional<std::string_view> config_dir) {
    if (system != "windows") {
        unsupported(system);
    }
    return Current(config_dir);
}

Current current(std::optional<std::string_view> config_dir) { return adapter_for(build_system(), config_dir); }
#else
void adapter_for(std::string_view system, std::optional<std::string_view>) { unsupported(system); }

void current(std::optional<std::string_view> config_dir) { adapter_for(build_system(), config_dir); }
#endif

}  // namespace qiven3::platform
