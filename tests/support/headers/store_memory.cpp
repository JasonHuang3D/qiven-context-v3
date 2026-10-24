// The memory store's unit for the analysis preset: the store and its scan
// on the fake platform, every member instantiated, and its import from its
// own export and from another source.

#include "store/memory.hpp"

#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::store {

using Fake = platform::fake::FakePlatform;

template class MemoryScan<Fake>;
template class MemoryStore<Fake>;
template Imported MemoryStore<Fake>::import_(state::MemoryExport&, Vector<LaneHead>&);
template Imported MemoryStore<Fake>::import_(detail::SourceArchetype&, Vector<LaneHead>&);

static_assert(Store<MemoryStore<Fake>>);

}  // namespace qiven3::store
