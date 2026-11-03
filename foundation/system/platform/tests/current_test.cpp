// current's choice of the build's adapter, on every system: adapter_for a
// system no adapter is built for refused, PlatformError unsupported from
// current, its detail naming the system; the build's own system chosen,
// the Windows adapter under _WIN32, and refused where no adapter is built.
// adapter_for takes no arena, by its signature, and its refusal holds its
// detail in place (platform.hpp's PlatformError).

#include <cstddef>
#include <optional>
#include <string_view>

#include "qiven/platform/current.hpp"
#include "qiven/platform/platform.hpp"
#include "qiven/runner/runner.hpp"

namespace {

using qiven::platform::Code;
using qiven::platform::PlatformError;

// The cases' own configuration leaf and stage's bound.
constexpr std::string_view leaf = "qiven-platform-test";
constexpr std::size_t open_bound = 16;

// Whether the call refuses: PlatformError unsupported from current, its
// detail naming system.
template <class Call>
bool refused(Call&& call, std::string_view system) {
    try {
        static_cast<void>(call());
    } catch (const PlatformError& error) {
        return error.code == Code::unsupported && error.operation.view() == "current" &&
               error.detail.view().find(system) != std::string_view::npos;
    }
    return false;
}

}  // namespace

// A system with no adapter built here, its configuration location given or
// not: unsupported, naming it.
QIVEN_CASE(current, unsupported) {
    for (const std::string_view system : {"linux", "macos", "plan9"}) {
        CHECK(refused([&] { return qiven::platform::adapter_for(system, leaf, open_bound, std::nullopt); }, system));
        CHECK(refused([&] { return qiven::platform::adapter_for(system, leaf, open_bound, "C:\\qiven"); }, system));
    }
}

// The build's own system: the Windows adapter under _WIN32, by
// adapter_for and by current; elsewhere current refused, naming it.
QIVEN_CASE(current, build_system) {
    const std::string_view system = qiven::platform::build_system();
#if defined(_WIN32)
    CHECK_EQ(system, std::string_view("windows"));
    CHECK_EQ(qiven::platform::current(leaf, open_bound, std::nullopt).name(), std::string_view("windows"));
    CHECK_EQ(qiven::platform::adapter_for("windows", leaf, open_bound, std::nullopt).name(),
             std::string_view("windows"));
#else
    CHECK(system == "linux" || system == "macos" || system == "unknown");
    CHECK(refused([] { qiven::platform::current(leaf, open_bound, std::nullopt); }, system));
#endif
}
