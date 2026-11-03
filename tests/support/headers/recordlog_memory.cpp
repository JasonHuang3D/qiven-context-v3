// The memory store's unit for the analysis preset: the store and its scan
// on the fake platform, every member instantiated, and its import from its
// own export and from another source.

#include "qiven/recordlog/memory.hpp"

#include "qiven/platform/fake/fake_platform.hpp"

namespace qiven::recordlog {

using Fake = qiven::platform::fake::FakePlatform;

template class MemoryScan<Fake>;
template class MemoryStore<Fake>;
template Imported MemoryStore<Fake>::import_(state::MemoryExport&, Vector<LaneHead>&, NoAdmit&&);
template Imported MemoryStore<Fake>::import_(detail::SourceArchetype&, Vector<LaneHead>&, NoAdmit&&);

static_assert(Store<MemoryStore<Fake>>);

}  // namespace qiven::recordlog
