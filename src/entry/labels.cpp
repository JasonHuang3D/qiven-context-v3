// labels' instantiation for v3's allocator: a version and a check read
// from an arena's header and content, compiled once here.

#include "entry/labels.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::labels {

using Alloc = memory::Allocator<char>;

template std::optional<VersionView> version_of(const header::Header<Alloc>&, const canon::Basic<Alloc>&);
template std::optional<CheckView> check_of(const header::Header<Alloc>&, const canon::Basic<Alloc>&, bool, Transcript);

}  // namespace qiven3::entry::labels
