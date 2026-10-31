// json's instantiation for v3's allocator: the value, scan and encode over
// an arena, compiled once here.

#include "qiven3/json.hpp"

namespace qiven3::json {

template class Basic<memory::Allocator<char>>;
template class Writer<memory::Allocator<char>>;
template std::optional<ParseCode> try_scan(std::string_view, std::size_t, Value&);
template void scan(std::string_view, std::size_t, Value&);
template void encode(const Value&, memory::String&);

}  // namespace qiven3::json
