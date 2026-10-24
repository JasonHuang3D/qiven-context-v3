// content_fields' instantiation for v3's allocator: the walk over an
// arena's content, compiled once here.

#include "entry/content_fields.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::content::detail {

template class WalkView<memory::Allocator<char>>;

}  // namespace qiven3::entry::content::detail
