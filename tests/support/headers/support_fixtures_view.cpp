// view's unit for the analysis preset: the fixtures' view of a store over
// the standard allocator and v3's, the two its cases give it.

#include "support/fixtures/view.hpp"

#include <memory>

#include "memory/memory.hpp"

namespace qiven3::test::fixtures {

using Std = std::allocator<char>;
using Chars = memory::Allocator<char>;

template struct Item<Std>;
template struct Item<Chars>;
template header::Header<Chars> copy(const header::Header<Std>&, const Chars&);
template header::Header<Std> copy(const header::Header<Chars>&, const Std&);
template void MemoryView::put(const header::Header<Std>&, std::optional<std::string_view>);
template void MemoryView::put(const header::Header<Chars>&, std::optional<std::string_view>);
template bool MemoryView::header(std::string_view, header::Header<Std>&);
template bool MemoryView::header(std::string_view, header::Header<Chars>&);
template bool MemoryView::body(std::string_view, validator::Text<Std>&);
template bool MemoryView::body(std::string_view, validator::Text<Chars>&);

}  // namespace qiven3::test::fixtures
