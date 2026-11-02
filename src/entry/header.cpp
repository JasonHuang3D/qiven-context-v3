// header's instantiation for v3's allocator: parse_line, make, check and
// line over an arena, compiled once here.

#include "entry/header.hpp"

#include "qiven/memory/memory.hpp"

namespace qiven3::entry::header {

using Alloc = qiven::memory::Allocator<char>;

template class Header<Alloc>;
template Header<Alloc> parse_line(std::string_view, const Alloc&);
template Header<Alloc> make(const FieldsView&, const Alloc&);
template std::optional<Code> try_check(const Header<Alloc>&);
template void check(const Header<Alloc>&);
template void Header<Alloc>::line(qiven::memory::String&) const;

}  // namespace qiven3::entry::header
