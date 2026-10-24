// line's unit for the analysis preset: the header, and encode_line into
// v3's string and the standard's, the two its callers give it.

#include "log/line.hpp"

#include <string>

#include "memory/memory.hpp"

namespace qiven3::log {

template void encode_line(const HeadView&, std::span<const FieldView>, memory::String&);
template void encode_line(const HeadView&, std::span<const FieldView>, std::string&);

}  // namespace qiven3::log
