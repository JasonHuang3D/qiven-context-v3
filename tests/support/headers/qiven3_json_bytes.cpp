// json_bytes' unit for the analysis preset: the scan and the encoding over
// the standard allocator, the constant-evaluated vectors' type; json.cpp
// compiles them over v3's.

#include "qiven3/json_bytes.hpp"

#include <memory>
#include <string>

namespace qiven3::json {

using Std = std::allocator<char>;

template class Basic<Std>;
template class Writer<Std>;
template class detail::PassView<Std>;
template std::optional<ParseCode> try_scan(std::string_view, std::size_t, Basic<Std>&);
template void scan(std::string_view, std::size_t, Basic<Std>&);
template void encode(const Basic<Std>&, std::string&);

}  // namespace qiven3::json
