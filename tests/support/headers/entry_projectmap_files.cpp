// projectmap_files' unit for the analysis preset: the map files' parser
// and the marks file's bytes over the standard allocator, the
// constant-evaluated vectors' type; projectmap.cpp compiles them over v3's.

#include "entry/projectmap_files.hpp"

#include <memory>
#include <string>

namespace qiven3::entry::projectmap {

using Std = std::allocator<char>;

template class Registry<Std>;
template class Marks<Std>;
template std::optional<FileError> try_parse_registry(std::string_view, Registry<Std>&, System);
template Registry<Std> parse_registry(std::string_view, const Std&, System);
template std::optional<FileError> try_parse_marks(std::string_view, Marks<Std>&);
template Marks<Std> parse_marks(std::string_view, const Std&);
template void marks_bytes(const Marks<Std>&, std::string&);

}  // namespace qiven3::entry::projectmap
