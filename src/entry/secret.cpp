// secret's instantiation for v3's allocator: the check over an arena's
// header and content, compiled once here.

#include "entry/secret.hpp"

#include <optional>

#include "memory/memory.hpp"

namespace qiven3::entry::secret {

using Alloc = memory::Allocator<char>;

template std::optional<SecretError> try_check(const header::Header<Alloc>&, const canon::Basic<Alloc>&,
                                              Hits<Alloc>&);
template Hits<Alloc> check(const header::Header<Alloc>&, const canon::Basic<Alloc>&);

}  // namespace qiven3::entry::secret
