// core's unit for the analysis preset: the runner's checks over the types
// its cases compare and print, a status, a size and text.

#include "qiven/runner/core.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace qiven::runner {

template std::string show(const int&);
template std::string show(const std::string&);
template bool check_eq(Context&, const int&, const int&, const char*, const char*, int);
template bool check_eq(Context&, const std::size_t&, const std::size_t&, const char*, const char*, int);
template bool check_eq(Context&, const std::string&, const std::string&, const char*, const char*, int);
template bool check_eq(Context&, const std::string&, const std::string_view&, const char*, const char*, int);

}  // namespace qiven::runner
