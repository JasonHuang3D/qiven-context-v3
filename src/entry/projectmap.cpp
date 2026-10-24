// projectmap's instantiation for v3's allocator: the map files' parser and
// the marks file's bytes over an arena, compiled once here. check_map is a
// template on the store's view and mark on the platform of the key,
// instantiated where a store or the map check links them.

#include "entry/projectmap.hpp"

#include "entry/projectmap_files.hpp"
#include "memory/memory.hpp"

namespace qiven3::entry::projectmap {

using Alloc = memory::Allocator<char>;

template std::optional<FileError> try_parse_registry(std::string_view, Registry<Alloc>&, System);
template std::optional<FileError> try_parse_marks(std::string_view, Marks<Alloc>&);
template void marks_bytes(const Marks<Alloc>&, memory::String&);

}  // namespace qiven3::entry::projectmap
