// The SQLite store's unit for the analysis preset: the store on the fake
// platform with the default hooks, as its cases make it, and the engine's
// heap there, every member instantiated, and its import.

#include "store/sqlite_store.hpp"

#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::store {

using Fake = platform::fake::FakePlatform;

template class Engine<Fake>;
template class SQLiteStore<Fake, NoHooks>;
template Imported SQLiteStore<Fake, NoHooks>::import_(detail::SourceArchetype&, Vector<LaneHead>&);

static_assert(Store<SQLiteStore<Fake, NoHooks>>);

}  // namespace qiven3::store
