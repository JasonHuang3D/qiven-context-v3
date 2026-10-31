// canon's instantiation for v3's allocator and the default key rule: the
// strict parser and the canonical bytes over an arena, compiled once here.

#include "qiven3/canon.hpp"

#include "memory/memory.hpp"

namespace qiven3::canon {

template std::optional<ParseCode> try_parse(std::string_view, Value&, const Syntax&, Layout);
template void parse(std::string_view, Value&, const Syntax&, Layout);
template std::optional<ParseCode> try_canonical(const Value&, memory::String&, const Syntax&);
template void canonical(const Value&, memory::String&, const Syntax&);

}  // namespace qiven3::canon
