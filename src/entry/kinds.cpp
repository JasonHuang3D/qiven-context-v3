// kinds' instantiation for v3's allocator: identity over an arena's header
// and content, compiled once here.

#include "entry/kinds.hpp"

#include "entry/kinds_identity.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/text/canon.hpp"

namespace qiven3::entry::kinds {

using Alloc = qiven::memory::Allocator<char>;

template std::optional<IdentityView> identity(const header::Header<Alloc>&, const canon::Basic<Alloc>&);

}  // namespace qiven3::entry::kinds
