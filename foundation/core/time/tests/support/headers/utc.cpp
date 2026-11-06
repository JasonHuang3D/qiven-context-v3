// utc's unit for the analysis preset: the header, and print_at into
// memory's string and the standard's, the two its callers give it.

#include "qiven/time/utc.hpp"

#include <string>

#include "qiven/memory/memory.hpp"

namespace qiven::time::utc {

template bool print_at(Millis, qiven::memory::String&);
template bool print_at(Millis, std::string&);

}  // namespace qiven::time::utc
