// The ruled stores' unit for the analysis preset: each store on the fake
// platform with the rules as its default hook, as its cases make it, every
// member instantiated, and its import.

#include "checked/stores.hpp"

#include "qiven/platform/fake/fake_platform.hpp"

namespace qiven::recordlog {

namespace checked = qiven3::checked;
using Fake = qiven::platform::fake::FakePlatform;

template class MemoryStore<Fake, checked::Rules<Fake>>;
template class SQLiteStore<Fake, NoHooks, checked::Rules<Fake>>;
template Imported SQLiteStore<Fake, NoHooks, checked::Rules<Fake>>::import_(detail::SourceArchetype&, Vector<LaneHead>&,
                                                                            checked::Rules<Fake>&&);

static_assert(Store<checked::MemoryStore<Fake>>);
static_assert(Store<checked::SQLiteStore<Fake>>);
static_assert(sizeof(checked::MemoryStore<Fake>) == sizeof(MemoryStore<Fake, checked::Rules<Fake>>),
              "a ruled store adds no member");
static_assert(sizeof(checked::SQLiteStore<Fake>) == sizeof(SQLiteStore<Fake, NoHooks, checked::Rules<Fake>>),
              "a ruled store adds no member");

}  // namespace qiven::recordlog

namespace qiven3::checked {

namespace store = qiven::recordlog;

using Fake = qiven::platform::fake::FakePlatform;

// The ruled reads over each store's lent ones, and their scans.
template class Ruled<store::MemoryStore<Fake, Rules<Fake>>>;
template class Ruled<store::SQLiteStore<Fake, store::NoHooks, Rules<Fake>>>;
template class Scan<store::MemoryScan<Fake>>;
template class Scan<store::sqlite::ScanView>;

}  // namespace qiven3::checked
