// refs' instantiation for v3's allocator: a reference in an arena's value,
// and a subject over an arena, compiled once here.

#include "entry/refs.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::refs {

using Alloc = memory::Allocator<char>;

template std::optional<RefError> try_check_reference(const canon::Basic<Alloc>&, json::Index);
template void check_reference(const canon::Basic<Alloc>&, json::Index);
template bool check_subject(std::string_view, const Alloc&);

}  // namespace qiven3::entry::refs
