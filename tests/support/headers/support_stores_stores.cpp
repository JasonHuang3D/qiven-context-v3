// stores' unit for the analysis preset: the factories on the fake
// platform over the plain stores, the SQLite one with the default hooks.

#include "support/stores/stores.hpp"

namespace qiven::test::stores {

template class MemoryOn<qiven3::store::MemoryStore<Fake>>;
template class SqliteOn<qiven3::store::SQLiteStore<Fake>, Site, qiven3::store::NoHooks>;
template std::optional<std::string> lacking<Memory>(const Needs&);
template std::optional<std::string> lacking<Sqlite<qiven3::store::NoHooks>>(const Needs&);
template void require<Memory>(const Needs&);
template void require<Sqlite<qiven3::store::NoHooks>>(const Needs&);

}  // namespace qiven::test::stores
