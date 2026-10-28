// qiven3: the binary's instantiation file. It instantiates the templates
// qiven3 calls for its one adapter, platform::Current, and gives main the
// adapter's maker, current(). Where no adapter is built there is no
// Current: main is instantiated for current() alone, which throws, and the
// binary exits 1, or 2 for a usage error (app/main.hpp).
//
// The command line reaches main as UTF-8. On Windows the binary's entry is
// wmain, which takes the arguments as UTF-16, as the system holds them,
// and the Windows adapter converts each strictly (run_utf8): an argument
// that is not valid UTF-16 is a request the wire cannot read, and exits 2
// with no line (wire::failed). Elsewhere main takes them as the system
// gives them, and the wire reads them.

#include <optional>
#include <span>
#include <string_view>

#include "app/main_impl.hpp"
#include "platform/current.hpp"
#include "qiven3/location_impl.hpp"
#include "wire/wire.hpp"

namespace qiven3 {

#if defined(_WIN32)
template int location::main<platform::Current>(std::span<const std::string_view>, platform::Current&,
                                               const location::Hooks&) noexcept;
#endif

template int app::main(int, const char* const*, decltype(&platform::current)) noexcept;

namespace {

int run(int argc, const char* const* argv) noexcept { return app::main(argc, argv, &platform::current); }

#if defined(_WIN32)
int run_wide(int argc, const wchar_t* const* argv) noexcept {
    return platform::win::run_utf8(argc, argv, &run, wire::failed);
}
#endif

}  // namespace
}  // namespace qiven3

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) { return qiven3::run_wide(argc, argv); }
#else
int main(int argc, char** argv) { return qiven3::run(argc, argv); }
#endif
