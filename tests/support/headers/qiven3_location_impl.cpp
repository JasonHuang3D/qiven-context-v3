// location_impl's unit for the analysis preset: location's templates on the
// fake platform, the one the cases give them; the binary's instantiation
// file gives them the build's adapter.

#include <span>
#include <string_view>

#include "qiven3/location_impl.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::location {

using Fake = platform::fake::FakePlatform;

template int main<Fake>(std::span<const std::string_view>, Fake&, const Hooks&) noexcept;
template void store_dir<Fake>(Fake&, String&);

}  // namespace qiven3::location
