// qiven3: the binary's instantiation file. It instantiates the templates
// qiven3 calls for its one adapter, platform::Current, and gives main the
// adapter's maker, current(). Where no adapter is built there is no
// Current: main is instantiated for current() alone, which throws, and the
// binary exits 1, or 2 for a usage error (app/main.hpp).

#include <optional>
#include <span>
#include <string_view>

#include "app/main_impl.hpp"
#include "platform/current.hpp"
#include "qiven3/location_impl.hpp"

namespace qiven3 {

#if defined(_WIN32)
template int location::main<platform::Current>(std::span<const std::string_view>, platform::Current&,
                                               const location::Hooks&) noexcept;
#endif

template int app::main(int, const char* const*, decltype(&platform::current)) noexcept;

namespace {

int run(int argc, const char* const* argv) noexcept { return app::main(argc, argv, &platform::current); }

}  // namespace
}  // namespace qiven3

int main(int argc, char** argv) { return qiven3::run(argc, argv); }
