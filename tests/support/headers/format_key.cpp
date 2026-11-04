// key's unit for the analysis preset: the header, and Key and its
// functions on the fake platform, with the keyed digest into memory's
// string.

#include "qiven3/format/key.hpp"

#include <span>
#include <string_view>

#include "qiven/memory/memory.hpp"
#include "qiven/platform/fake/fake_platform.hpp"

namespace qiven3::format {

using Fake = qiven::platform::fake::FakePlatform;

template class Key<Fake>;
template void keyed(const Key<Fake>&, std::string_view, qiven::memory::String&);
template void key_hex(const Key<Fake>&, std::span<char, 2 * qiven::digest::bytes>) noexcept;
template Key<Fake> key_from_hex(Fake&, std::string_view) noexcept;
template Key<Fake> new_key(Fake&);

}  // namespace qiven3::format
