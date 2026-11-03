// json's instantiation for memory's allocator: the value, scan and encode over
// an arena, compiled once here.

#include "qiven/text/json.hpp"

namespace qiven::text::json {

template class Basic<qiven::memory::Allocator<char>>;
template class Writer<qiven::memory::Allocator<char>>;
template std::optional<ParseCode> try_scan(std::string_view, std::size_t, Value&);
template void scan(std::string_view, std::size_t, Value&);
template void encode(const Value&, qiven::memory::String&);

}  // namespace qiven::text::json
