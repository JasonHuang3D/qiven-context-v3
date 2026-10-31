// The SQLite store's bodies' unit for the analysis preset: their one includer,
// the store's header, then a write of each kind on the fake platform with the
// default hooks, so that append's and forget's steps are instantiated.

#include "store/sqlite_store.hpp"
#include "store/sqlite_store_impl.hpp"

#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::store {

using Fake = platform::fake::FakePlatform;

template Written SQLiteStore<Fake, NoHooks>::append(std::string_view, std::string_view, Expected,
                                                    std::span<const EntryView>, NoAdmit&&);
template Written SQLiteStore<Fake, NoHooks>::forget(std::string_view, std::string_view, Expected,
                                                    std::span<const EntryView>, std::span<const GuardView>,
                                                    Vector<Id>&, NoAdmit&&);

}  // namespace qiven3::store
