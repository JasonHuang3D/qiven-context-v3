// canon's instantiation for memory's allocator and the key rule Syntax:
// the strict parser and the canonical bytes over an arena, compiled once
// here.

#include "qiven/text/canon.hpp"

#include "qiven/memory/memory.hpp"

namespace qiven::text::canon {

template std::optional<ParseCode> try_parse(std::string_view, Value&, const Syntax&, std::size_t, Layout);
template void parse(std::string_view, Value&, const Syntax&, std::size_t, Layout);
template std::optional<ParseCode> try_canonical(const Value&, qiven::memory::String&, const Syntax&);
template void canonical(const Value&, qiven::memory::String&, const Syntax&);

}  // namespace qiven::text::canon
