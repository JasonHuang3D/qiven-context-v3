// contract's unit for the analysis preset: the header, its concepts met by
// their archetypes.

#include "store/contract.hpp"

namespace qiven3::store {

static_assert(Source<detail::SourceArchetype> && Hooks<NoHooks>);

}  // namespace qiven3::store
