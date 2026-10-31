// kinds' instantiation for v3's allocator: identity over an arena's header
// and content, compiled once here.

#include "entry/kinds.hpp"

#include "entry/kinds_identity.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::kinds {

using Alloc = memory::Allocator<char>;

template std::optional<IdentityView> identity(const header::Header<Alloc>&, const canon::Basic<Alloc>&);

}  // namespace qiven3::entry::kinds
