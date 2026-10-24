// stores' unit for the analysis preset: the factories on the fake
// platform, the SQLite one with the default hooks.

#include "support/stores/stores.hpp"

namespace qiven3::test::stores {

template class Sqlite<store::NoHooks>;
template std::optional<std::string> lacking<Memory>(const Needs&);
template std::optional<std::string> lacking<Sqlite<store::NoHooks>>(const Needs&);
template void require<Memory>(const Needs&);
template void require<Sqlite<store::NoHooks>>(const Needs&);

}  // namespace qiven3::test::stores
