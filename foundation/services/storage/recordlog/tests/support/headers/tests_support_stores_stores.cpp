// stores' unit for the analysis preset: the factories on the fake
// platform over the plain stores, the SQLite one with the default hooks.

#include "support/stores/stores.hpp"

namespace qiven::test::stores {

template class MemoryOn<qiven::recordlog::MemoryStore<Fake>>;
template class SqliteOn<qiven::recordlog::SQLiteStore<Fake>, Site, qiven::recordlog::NoHooks>;
template std::optional<std::string> lacking<Memory>(const Needs&);
template std::optional<std::string> lacking<Sqlite<qiven::recordlog::NoHooks>>(const Needs&);
template void require<Memory>(const Needs&);
template void require<Sqlite<qiven::recordlog::NoHooks>>(const Needs&);

}  // namespace qiven::test::stores
