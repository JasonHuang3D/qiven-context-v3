// fold's instantiation for v3's allocator: the fold's work in an arena,
// and an entry read from an arena's header and content, compiled once here.

#include "entry/fold.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::fold {

using Alloc = memory::Allocator<char>;

template State fold(std::span<const ItemView>, std::string_view, const Alloc&);
template ItemView item(const header::Header<Alloc>&, const canon::Basic<Alloc>&, bool);

}  // namespace qiven3::entry::fold
