// validator's instantiation for v3's allocator: the identity rule over an
// arena's headers and contents, compiled once here. validate itself is a
// template on the store's view and the call's entries, instantiated where
// a store links it.

#include "entry/validator.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::validator {

using Alloc = memory::Allocator<char>;

template bool detail::same_identity(const header::Header<Alloc>&, const canon::Basic<Alloc>&,
                                    const header::Header<Alloc>&, const canon::Basic<Alloc>&);

}  // namespace qiven3::entry::validator
