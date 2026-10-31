// store/sqlite's page cache, on the fake, over a scratch directory of the
// case's own, the rig of sqlite_rig.hpp. cache: a connection's page cache
// on the engine's slots, on a fresh engine heap for each read of a plain
// database of 1,500 rows of 3,000 bytes, a row a page: a slot each cached
// page, none of the heap's,
// the full cache's bytes and the connection's peak within the engine's
// budget for a connection; cache_shared: two connections' caches at once,
// within the slots and past them; slots_refused: an engine heap refused
// while one is installed, its slots kept, or by its configuration's
// failed call, none kept.

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "support/stores/sqlite_rig.hpp"

using namespace qiven3::test::sqlite_rig;  // NOLINT(google-build-using-namespace)

namespace {

// sql read on a fresh connection over a fresh engine heap.
Held cache_after(Rig& rig, const std::string& path, std::string_view sql) {
    rig.fresh_engine();
    sqlite::Connection c = raw(path);
    (void)c.integer(sql, test_at);
    return held(rig);
}

// A plain database of 1,500 rows of 3,000 bytes, a row a page.
constexpr std::string_view all_rows = "SELECT sum(length(x)) FROM t";
constexpr std::int64_t all_bytes = std::int64_t{1500} * 3000;
std::string plain(Rig& rig) {
    const std::string path = text(rig.root / "plain.db");
    std::ofstream(path, std::ios::binary).flush();  // 0 bytes: an empty database
    sqlite::Connection c = raw(path);
    c.exec("CREATE TABLE t (x BLOB)", test_at);
    c.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 1500) "
           "INSERT INTO t SELECT zeroblob(3000) FROM n",
           test_at);
    return path;
}

// A slot's bytes, and a full cache's slots (its pages and the
// connection's page buffers) as cache_slots, as measured on each
// system's build of the engine, whose per-page header is its own;
// elsewhere not compared.
#if defined(_WIN32) || defined(__linux__)
constexpr bool slots_pinned = true;
#else
constexpr bool slots_pinned = false;
#endif
constexpr std::size_t slot_measured = 4368;

}  // namespace

QIVEN3_CASE(store_sqlite, cache) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = plain(*rig);
    const Held one = cache_after(*rig, path, "SELECT length(x) FROM t WHERE rowid = 1");
    const Held hundred = cache_after(*rig, path, "SELECT sum(length(x)) FROM t WHERE rowid <= 100");
    const Held two_hundred = cache_after(*rig, path, "SELECT sum(length(x)) FROM t WHERE rowid <= 200");
    const Held all = cache_after(*rig, path, all_rows);
    const sqlite::Slots& s = all.slots;
    const std::size_t slots_a_page = two_hundred.slots.used - hundred.slots.used;  // of 100 pages
    const std::size_t heap_a_page = (two_hundred.used - hundred.used) / 100;
    const std::size_t cache = all.in_slots() + s.overflow;
    std::cout << "store_sqlite.cache: a slot " << s.size << " bytes, " << s.count << " slots for "
              << sqlite::default_connections << " connections; one row " << one.slots.used << " slots, heap "
              << one.used << "; 100 rows " << hundred.slots.used << ", heap " << hundred.used << "; 200 rows "
              << two_hundred.slots.used << ", heap " << two_hundred.used << ": " << slots_a_page
              << " slots and " << heap_a_page << " heap bytes a 100 pages' page; 1,500 rows " << s.used
              << " slots (mark " << s.peak << "), heap " << all.used << " (peak " << all.peak << "), past the slots "
              << s.overflow << " (mark " << s.overflow_peak << "): the cache " << cache << " against 2,000 KiB ("
              << std::size_t{2000} * 1024 << "); the connection's peak " << all.top()
              << " against the engine's budget a connection " << qiven3::store::engine_budget << "\n";
    // Each cached page on a slot of its own, the page and the engine's
    // header, none on the heap's classes: a full cache holds about its
    // 2,000 KiB, the connection within its budget.
    CHECK_EQ(s.count, (sqlite::cache_slots * sqlite::default_connections) + sqlite::slots_reserve);
    CHECK_EQ(rig->engine->heap().budget() + (s.count * s.size),
             qiven3::store::engine_budget * sqlite::default_connections);  // the heap before the slots
    CHECK(s.size > sqlite::page_size && s.size < 2 * sqlite::page_size && s.size % 8 == 0);
    CHECK_EQ(slots_a_page, 100U);
    CHECK(heap_a_page < 64);
    CHECK_EQ(one.slots.peak, one.slots.used);  // the marks from the fresh engine heap on
    CHECK_EQ(s.overflow_peak, 0U);
    CHECK(s.used <= sqlite::cache_slots);
    CHECK(cache <= sqlite::cache_slots * s.size);
    CHECK(all.top() <= qiven3::store::engine_budget);
    if (slots_pinned) {
        CHECK_EQ(s.size, slot_measured);
        CHECK_EQ(s.used, sqlite::cache_slots);  // a full cache, as cache_slots holds it
    }
}

QIVEN3_CASE(store_sqlite, cache_shared) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = plain(*rig);
    for (const std::size_t connections : {sqlite::default_connections, std::size_t{1}}) {
        rig->fresh_engine(connections);
        sqlite::Connection a = raw(path);
        sqlite::Connection b = raw(path);
        CHECK_EQ(a.integer(all_rows, test_at), all_bytes);
        const Held first = held(*rig);
        CHECK_EQ(b.integer(all_rows, test_at), all_bytes);
        CHECK_EQ(a.integer(all_rows, test_at), all_bytes);
        const Held both = held(*rig);
        const sqlite::Slots& s = both.slots;
        std::cout << "store_sqlite.cache_shared: " << connections << " connections' engine heap (" << s.count
                  << " slots, heap " << rig->engine->heap().budget() << "), two caches full at once: first "
                  << first.slots.used << " slots, then " << s.used << " (mark " << s.peak << "), heap " << both.used
                  << " (peak " << both.peak << "), past the slots " << s.overflow << " (mark " << s.overflow_peak
                  << "): peak " << both.top() << " against " << qiven3::store::engine_budget * connections
                  << " for its connections, " << qiven3::store::engine_budget * 2 << " for two\n";
        CHECK(both.top() <= qiven3::store::engine_budget * 2);
        // a's full cache and b's first page buffer, the engine's reserve
        // beside the slots letting a's cache fill
        CHECK(!slots_pinned || first.slots.used == sqlite::cache_slots + 1);
        if (connections >= 2) {
            CHECK(!slots_pinned || s.used == 2 * sqlite::cache_slots);
            CHECK_EQ(s.overflow_peak, 0U);
        } else {
            // Past its last free slots the engine recycles a cache's pages;
            // pages it cannot recycle, a temporary table's in memory, it
            // takes from the heap, a class of 8,192 each, within the
            // heap's budget.
            CHECK(s.peak <= s.count);
            a.exec("CREATE TEMP TABLE m AS SELECT x FROM t WHERE rowid <= 60", test_at);
            const Held past = held(*rig);
            std::cout << "store_sqlite.cache_shared: then a temporary table of 60 pages: " << past.slots.used
                      << " slots, past the slots " << past.slots.overflow << " (mark " << past.slots.overflow_peak
                      << "), heap " << past.used << " (peak " << past.peak << ") of " << rig->engine->heap().budget()
                      << "\n";
            CHECK(past.slots.overflow_peak >= past.slots.overflow);
            CHECK(past.slots.overflow >= 50 * 2 * sqlite::page_size);
            CHECK(past.peak <= rig->engine->heap().budget());
        }
    }
}

QIVEN3_CASE(store_sqlite, slots_refused) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = plain(*rig);
    const sqlite::Slots before = rig->engine->slots();
    CHECK_EQ(raised([&] { const Engine second(rig->fake); }),
             "StoreError internal: an engine heap is installed already");
    {
        sqlite::Connection c = raw(path);
        CHECK_EQ(c.integer(all_rows, test_at), all_bytes);
        const sqlite::Slots after = rig->engine->slots();
        CHECK_EQ(after.count, before.count);
        CHECK_EQ(after.size, before.size);
        CHECK(after.used > sqlite::cache_slots / 2);
    }
    // The slots' pages refused: nothing installed, the next engine heap made.
    rig->engine.reset();
    rig->fake.refuse_pages(1);
    std::string refused = "none";
    try {
        const Engine third(rig->fake);
    } catch (const qiven3::platform::PlatformError&) {
        refused = "PlatformError";
    }
    CHECK_EQ(refused, "PlatformError");
    CHECK(!sqlite::engine_ready());
    // Pages short of the slots: refused, nothing installed.
    {
        std::vector<std::byte> short_pages(qiven3::store::engine_budget / 2);
        CHECK_EQ(raised([&] { const sqlite::EngineHeap h(short_pages, 1); }),
                 "StoreError internal: the engine heap's pages cannot hold the page cache's slots");
        CHECK(!sqlite::engine_ready());
    }
#if defined(QIVEN3_FAULTS)
    // The slots' configuration failed: the engine shut down, none kept.
    qiven3::memory::Countdown calls;
    calls.arm(1);
    sqlite::attach_calls(&calls, nullptr);
    refused = raised([&] { const Engine fourth(rig->fake); });
    sqlite::attach_calls(nullptr, nullptr);
    std::cout << "store_sqlite.slots_refused: " << refused << "\n";
    CHECK(calls.fired());
    CHECK(refused.starts_with("StoreError internal: SQLITE_IOERR"));
    CHECK(!sqlite::engine_ready());
#endif
    rig->fresh_engine();
    sqlite::Connection c = raw(path);
    CHECK_EQ(c.integer(all_rows, test_at), all_bytes);
    CHECK_EQ(rig->engine->slots().count, before.count);
    CHECK(rig->engine->slots().used > sqlite::cache_slots / 2);
}
