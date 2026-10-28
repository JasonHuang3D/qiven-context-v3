// kinds_identity's unit for the analysis preset: identity over the
// standard allocator's header and content, the constant-evaluated vectors'
// type; kinds.cpp compiles it over v3's.

#include "entry/kinds_identity.hpp"

#include <memory>
#include <optional>

#include "entry/header.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::kinds {

using Std = std::allocator<char>;

template std::optional<IdentityView> identity(const header::Header<Std>&, const canon::Basic<Std>&);

}  // namespace qiven3::entry::kinds
