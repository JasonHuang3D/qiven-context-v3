// utc's unit for the analysis preset: the header, and print_at into v3's
// string and the standard's, the two its callers give it.

#include "qiven3/utc.hpp"

#include <string>

#include "memory/memory.hpp"

namespace qiven3::utc {

template bool print_at(Millis, memory::String&);
template bool print_at(Millis, std::string&);

}  // namespace qiven3::utc
