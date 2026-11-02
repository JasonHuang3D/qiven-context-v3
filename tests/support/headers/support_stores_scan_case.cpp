// scan_case's unit for the analysis preset: the case on each plain store
// on the fake platform, as their case files bind it.

#include "support/stores/scan_case.hpp"

#include "store/memory.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven::test::scan_case {

using Fake = qiven3::platform::fake::FakePlatform;
using Memory = qiven3::store::MemoryStore<Fake>;
using SQLite = qiven3::store::SQLiteStore<Fake>;

template void scan_ends_at_start(qiven::runner::Context&, Memory&, Memory&, const Chars&);
template void scan_ends_at_start(qiven::runner::Context&, SQLite&, SQLite&, const Chars&);

}  // namespace qiven::test::scan_case
