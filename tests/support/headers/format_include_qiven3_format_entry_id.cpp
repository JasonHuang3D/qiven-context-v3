// entry_id's unit for the analysis preset: the header, and the framing and
// the id into the standard's string, which its cases give it; memory's
// string's are format's source's.

#include "qiven3/format/entry_id.hpp"

#include <span>
#include <string>

namespace qiven3::format {

template void frame(std::span<const Field>, std::string&);
template void entry_id(std::span<const Field>, std::string&);

}  // namespace qiven3::format
