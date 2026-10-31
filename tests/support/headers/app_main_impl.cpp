// main_impl's unit for the analysis preset: the binary's main on the fake
// platform's maker, and on a maker that makes none, as current() is where
// no adapter is built; the binary's instantiation file gives it current().

#include <optional>
#include <string_view>

#include "app/main_impl.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3 {
namespace app {

using Fake = platform::fake::FakePlatform;

Fake fake_maker(std::optional<std::string_view> config_dir);
[[noreturn]] void no_maker(std::optional<std::string_view> config_dir);

template int main(int, const char* const*, decltype(&fake_maker)) noexcept;
template int main(int, const char* const*, decltype(&no_maker)) noexcept;

}  // namespace app
}  // namespace qiven3
