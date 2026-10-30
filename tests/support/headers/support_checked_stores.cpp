// The ruled factory's unit for the analysis preset: the store's factories
// over the ruled stores on the fake platform, the SQLite one with the
// default hooks.

#include "support/checked/stores.hpp"

namespace qiven3::test::stores {

template class MemoryOn<checked::MemoryStore<Fake>, ruled::Site>;
template class SqliteOn<checked::SQLiteStore<Fake>, ruled::Site, store::NoHooks>;
template std::optional<std::string> lacking<ruled::Memory>(const Needs&);
template std::optional<std::string> lacking<ruled::Sqlite<store::NoHooks>>(const Needs&);
template void require<ruled::Memory>(const Needs&);
template void require<ruled::Sqlite<store::NoHooks>>(const Needs&);

}  // namespace qiven3::test::stores
