// served's instantiation for v3's allocator: a block, its cost and its
// stub from an arena's header and content into an arena's string,
// compiled once here.

#include "entry/served.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::served {

using Alloc = memory::Allocator<char>;

template void block(const header::Header<Alloc>&, const canon::Basic<Alloc>&, const State&, const labels::Label&,
                    const std::optional<MapMark>&, memory::String&);
template std::size_t block_cost(const header::Header<Alloc>&, const canon::Basic<Alloc>&, const State&,
                                const labels::Label&, const std::optional<MapMark>&);
template void stub(const header::Header<Alloc>&, const canon::Basic<Alloc>&, const State&, const labels::Label&,
                   const std::optional<MapMark>&, memory::String&);

}  // namespace qiven3::entry::served
