// The ruled factory's unit for the analysis preset: the store's factories
// over the ruled stores on the fake platform, the SQLite one with the
// default hooks.

#include "support/checked/stores.hpp"

namespace qiven::test::stores {

template class MemoryOn<qiven3::checked::MemoryStore<Fake>, ruled::Site>;
template class SqliteOn<qiven3::checked::SQLiteStore<Fake>, ruled::Site, qiven::recordlog::NoHooks>;
template std::optional<std::string> lacking<ruled::Memory>(const Needs&);
template std::optional<std::string> lacking<ruled::Sqlite<qiven::recordlog::NoHooks>>(const Needs&);
template void require<ruled::Memory>(const Needs&);
template void require<ruled::Sqlite<qiven::recordlog::NoHooks>>(const Needs&);

}  // namespace qiven::test::stores
