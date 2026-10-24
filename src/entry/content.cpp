// content's instantiation for v3's allocator: the check over an arena's
// header and content, compiled once here.

#include "entry/content.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::content {

using Alloc = memory::Allocator<char>;

template std::optional<ContentError> try_check(const header::Header<Alloc>&, const canon::Basic<Alloc>&);
template void check(const header::Header<Alloc>&, const canon::Basic<Alloc>&);

}  // namespace qiven3::entry::content
