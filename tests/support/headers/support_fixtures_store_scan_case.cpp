// store_scan_case's unit for the analysis preset: the case on each store
// on the fake platform, as their case files bind it.

#include "support/fixtures/store_scan_case.hpp"

#include "store/memory.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven3::test::scan_case {

using Fake = platform::fake::FakePlatform;
using Memory = store::MemoryStore<Fake>;
using SQLite = store::SQLiteStore<Fake>;

template void scan_ends_at_start(Context&, Memory&, Memory&, const stage_book::Book&, const Chars&);
template void scan_ends_at_start(Context&, SQLite&, SQLite&, const stage_book::Book&, const Chars&);

}  // namespace qiven3::test::scan_case
