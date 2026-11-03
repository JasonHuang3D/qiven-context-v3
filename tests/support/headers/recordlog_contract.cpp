// contract's unit for the analysis preset: the header, its concepts met by
// their archetypes.

#include "qiven/recordlog/contract.hpp"

namespace qiven::recordlog {

static_assert(Source<detail::SourceArchetype> && Hooks<NoHooks>);

}  // namespace qiven::recordlog
