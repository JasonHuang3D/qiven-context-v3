// qiven3: the binary's instantiation file. It instantiates the templates
// qiven3 calls for its one adapter, qiven::platform::Current, and gives main its
// maker, make_platform: current() with cognition's own configuration leaf,
// qiven3, and stage bound, 16. Where no adapter is built there is no Current: main is
// instantiated for a maker that calls current() alone, which throws, and
// the binary exits 1, or 2 for a usage error (app/main.hpp).
//
// The command line reaches main as UTF-8. On Windows the binary's entry is
// wmain, which takes the arguments as UTF-16, as the system holds them,
// and the Windows adapter converts each after the program's name, which it
// does not read, strictly (run_utf8): an argument that is not valid UTF-16,
// or a command line past its bounds, is a request the wire cannot read,
// and exits 2 with no line (wire::failed); an allocation refused while it
// is read is a fault, and exits 1 with no line (app::refused): no platform
// is made yet to write the fault's answer. Elsewhere main takes them as
// the system gives them, and the wire reads them.

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "app/main_impl.hpp"
#include "qiven/platform/current.hpp"
#include "qiven3/location_impl.hpp"
#include "wire/wire.hpp"

namespace qiven3 {

#if defined(_WIN32)
template int location::main<qiven::platform::Current>(std::span<const std::string_view>, qiven::platform::Current&,
                                               const location::Hooks&) noexcept;
#endif

namespace {

// Cognition's own configuration leaf: on Windows, LocalAppData\qiven3.
constexpr std::string_view config_leaf = "qiven3";
// Cognition's own stage bound (qiven/platform/files.hpp): sized for a
// caller that holds two files open at once; the rest is the margin its
// other budgets keep.
constexpr std::size_t open_bound = 16;

#if defined(_WIN32)
qiven::platform::Current make_platform(std::optional<std::string_view> config_dir) {
    return qiven::platform::current(config_leaf, open_bound, config_dir);
}
#else
[[noreturn]] void make_platform(std::optional<std::string_view> config_dir) {
    qiven::platform::current(config_leaf, open_bound, config_dir);
}
#endif

}  // namespace

template int app::main(int, const char* const*, decltype(&make_platform)) noexcept;

namespace {

int run(int argc, const char* const* argv) noexcept { return app::main(argc, argv, &make_platform); }

#if defined(_WIN32)
int run_wide(int argc, const wchar_t* const* argv) noexcept {
    return qiven::platform::win::run_utf8(argc, argv, &run, {wire::failed, app::refused});
}
#endif

}  // namespace
}  // namespace qiven3

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) { return qiven3::run_wide(argc, argv); }
#else
int main(int argc, char** argv) { return qiven3::run(argc, argv); }
#endif
