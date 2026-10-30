// The ruled stores' unit for the analysis preset: each store on the fake
// platform with the rules as its default hook, as its cases make it, every
// member instantiated, and its import.

#include "checked/stores.hpp"

#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::store {

using Fake = platform::fake::FakePlatform;

template class MemoryStore<Fake, checked::Rules<Fake>>;
template class SQLiteStore<Fake, NoHooks, checked::Rules<Fake>>;
template Imported SQLiteStore<Fake, NoHooks, checked::Rules<Fake>>::import_(detail::SourceArchetype&, Vector<LaneHead>&);

static_assert(Store<checked::MemoryStore<Fake>>);
static_assert(Store<checked::SQLiteStore<Fake>>);
static_assert(sizeof(checked::MemoryStore<Fake>) == sizeof(MemoryStore<Fake, checked::Rules<Fake>>),
              "a ruled store adds no member");
static_assert(sizeof(checked::SQLiteStore<Fake>) == sizeof(SQLiteStore<Fake, NoHooks, checked::Rules<Fake>>),
              "a ruled store adds no member");

}  // namespace qiven3::store
