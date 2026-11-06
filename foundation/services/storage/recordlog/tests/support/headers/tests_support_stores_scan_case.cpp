// scan_case's unit for the analysis preset: the case on each plain store
// on the fake platform, as their case files bind it.

#include "support/stores/scan_case.hpp"

#include "qiven/recordlog/memory.hpp"
#include "qiven/recordlog/sqlite_store.hpp"
#include "qiven/platform/fake/fake_platform.hpp"

namespace qiven::test::scan_case {

using Fake = qiven::platform::fake::FakePlatform;
using Memory = qiven::recordlog::MemoryStore<Fake>;
using SQLite = qiven::recordlog::SQLiteStore<Fake>;

template void scan_ends_at_start(qiven::runner::Context&, Memory&, Memory&, const Chars&);
template void scan_ends_at_start(qiven::runner::Context&, SQLite&, SQLite&, const Chars&);

}  // namespace qiven::test::scan_case
