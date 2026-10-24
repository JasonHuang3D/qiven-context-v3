// served_text's unit for the analysis preset: the text's writer over v3's
// string and the standard's, and a block, its cost and its stub over the
// standard allocator, the constant-evaluated vectors' type; served.cpp
// compiles them over v3's.

#include "entry/served_text.hpp"

#include <memory>
#include <string>

#include "entry/served.hpp"
#include "memory/memory.hpp"

namespace qiven3::entry::served {

using Std = std::allocator<char>;

template class detail::Write<std::string>;
template class detail::Write<memory::String>;
template void block(const header::Header<Std>&, const canon::Basic<Std>&, const State&, const labels::Label&,
                    const std::optional<MapMark>&, std::string&);
template std::size_t block_cost(const header::Header<Std>&, const canon::Basic<Std>&, const State&,
                                const labels::Label&, const std::optional<MapMark>&);
template void stub(const header::Header<Std>&, const canon::Basic<Std>&, const State&, const labels::Label&,
                   const std::optional<MapMark>&, std::string&);

}  // namespace qiven3::entry::served
