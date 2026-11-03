// storage_sqlite's own cases: the engine heap, its page cache and its
// refusals, and the engine's full-text build, over databases of the case's
// own in a scratch directory of its own, on an engine heap over pages the
// case takes itself, each setting a case needs written by the case. cache:
// a connection's page cache on the engine's slots, on a fresh engine heap
// for each read of a plain database of 1,500 rows of 3,000 bytes, a row a
// page: a slot each cached page, none of the heap's, the full cache's bytes
// and the connection's peak within the case's budget for a connection;
// cache_shared: two connections' caches at once, within the slots and past
// them; fts5: the engine's full-text index, an fts5 table on a plain
// database found by MATCH; slots_refused: an engine heap refused while one
// is installed, its slots kept, on pages short of its slots, or by its
// configuration's failed call, none kept, each EngineError with its
// refusal, its code, its detail and no site; refused_at: calls refused at
// their caller's site, an open with no engine heap, an engine call's
// failure with the engine's extended code, an empty statement and a query
// with no row, each EngineError with its refusal, its code, its detail and
// that site, a site past its bytes cut at a code point.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/memory/pool.hpp"
#include "qiven/runner/runner.hpp"
#include "qiven/storage_sqlite/connection.hpp"

namespace {

namespace fs = std::filesystem;
namespace sqlite = qiven::storage_sqlite;
using qiven::memory::Arena;
using qiven::memory::String;
using Chars = qiven::memory::Allocator<char>;

constexpr sqlite::Site test_at{"test", "scratch"};

// The case's settings: a page of the wrapper's page_size, written when a
// database is made; a connection's cache of 2,000 KiB, written at each
// open; and the case's budget a connection on the engine heap, that cache
// and a MiB beside it.
static_assert(sqlite::page_size == 4096);
constexpr std::string_view page_setting = "PRAGMA page_size = 4096";
constexpr std::string_view cache_setting = "PRAGMA cache_size = -2000";
constexpr std::size_t kib = 1024;
constexpr std::size_t budget = (2000 * kib) + (kib * kib);
// The busy wait of the case's connections, which never contend.
constexpr int no_wait = 0;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// The scratch directory, the engine heap's pages and the engine heap, made
// again at will; on the heap.
struct Rig {
    Rig() : root(fresh_root()) { fresh_engine(); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
    ~Rig() {
        engine.reset();
        std::error_code error;
        fs::remove_all(root, error);
    }
    static fs::path fresh_root() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-storage-sqlite-" + std::to_string(draw()) +
                                                    std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    // A fresh engine heap on fresh pages, the case's budget for each of its
    // connections: every connection on the last closed first.
    void fresh_engine(std::size_t connections = sqlite::default_connections) {
        engine.reset();
        pages.assign(budget * connections, std::byte{0});
        engine = std::make_unique<sqlite::EngineHeap>(pages, connections);
    }

    fs::path root;
    std::vector<std::byte> pages;  // before the engine heap, which is made over them and ends first
    std::unique_ptr<sqlite::EngineHeap> engine;
};

// A connection to the database at path, no statement run on it yet.
sqlite::Connection connected(const std::string& path) {
    std::vector<std::byte> bytes(4096);
    Arena arena(bytes);
    const String name(path, Chars(arena));
    return sqlite::Connection::open(name, false, no_wait, test_at);
}

// A connection to the database at path, its cache as the case sets it;
// the setting reads the database's schema, a page of its cache.
sqlite::Connection opened(const std::string& path) {
    sqlite::Connection c = connected(path);
    c.exec(cache_setting, test_at);
    return c;
}

// A connection to an empty database made at path, its page as the case
// sets it, which the database's first write takes.
sqlite::Connection made(const std::string& path) {
    std::ofstream(path, std::ios::binary).flush();  // 0 bytes: an empty database
    sqlite::Connection c = opened(path);
    c.exec(page_setting, test_at);
    return c;
}

// The EngineError a call raised; none, nullopt.
template <class F>
std::optional<sqlite::EngineError> raised(const F& f) {
    try {
        f();
    } catch (const sqlite::EngineError& e) {
        return e;
    }
    return std::nullopt;
}

// A call refused: its refusal, its code, its detail and the site it names;
// an engine heap's making, no site.
void refused_as(qiven::runner::Context& qiven_ctx, const std::optional<sqlite::EngineError>& e,
                sqlite::Refusal refusal, int code, std::string_view detail, const sqlite::Site& site = {}) {
    REQUIRE(e.has_value());
    CHECK(e->refusal() == refusal);
    CHECK_EQ(e->code(), code);
    CHECK_EQ(e->operation(), site.operation);
    CHECK_EQ(e->path(), site.path);
    CHECK_EQ(e->detail(), detail);
}

// The engine's bytes once a read: the heap's, held and their mark, and
// the page cache's slots.
struct Held {
    std::size_t used = 0;
    std::size_t peak = 0;
    sqlite::Slots slots;
    // The slots' bytes held and their mark; the connection's peak, the
    // heap's and the slots' marks together.
    [[nodiscard]] std::size_t in_slots() const { return slots.used * slots.size; }
    [[nodiscard]] std::size_t top() const { return peak + (slots.peak * slots.size); }
};
Held held(const Rig& rig) { return {rig.engine->heap().used(), rig.engine->heap().peak(), rig.engine->slots()}; }

// sql read on a fresh connection over a fresh engine heap.
Held cache_after(Rig& rig, const std::string& path, std::string_view sql) {
    rig.fresh_engine();
    sqlite::Connection c = opened(path);
    (void)c.integer(sql, test_at);
    return held(rig);
}

// A plain database of 1,500 rows of 3,000 bytes, a row a page.
constexpr std::string_view all_rows = "SELECT sum(length(x)) FROM t";
constexpr std::int64_t all_bytes = std::int64_t{1500} * 3000;
std::string plain(Rig& rig) {
    const std::string path = text(rig.root / "plain.db");
    sqlite::Connection c = made(path);
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

// The engine's I/O error code, SQLITE_IOERR, which the engine calls'
// countdown gives a failed call.
constexpr int io_error = 10;
// The engine's extended code of a value a unique column holds already,
// SQLITE_CONSTRAINT_UNIQUE.
constexpr int unique_held = 2067;

}  // namespace

QIVEN_CASE(storage_sqlite, cache) {
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
    std::cout << "storage_sqlite.cache: a slot " << s.size << " bytes, " << s.count << " slots for "
              << sqlite::default_connections << " connections; one row " << one.slots.used << " slots, heap "
              << one.used << "; 100 rows " << hundred.slots.used << ", heap " << hundred.used << "; 200 rows "
              << two_hundred.slots.used << ", heap " << two_hundred.used << ": " << slots_a_page
              << " slots and " << heap_a_page << " heap bytes a 100 pages' page; 1,500 rows " << s.used
              << " slots (mark " << s.peak << "), heap " << all.used << " (peak " << all.peak << "), past the slots "
              << s.overflow << " (mark " << s.overflow_peak << "): the cache " << cache << " against 2,000 KiB ("
              << std::size_t{2000} * kib << "); the connection's peak " << all.top()
              << " against the case's budget a connection " << budget << "\n";
    // Each cached page on a slot of its own, the page and the engine's
    // header, none on the heap's classes: a full cache holds about its
    // 2,000 KiB, the connection within its budget.
    CHECK_EQ(s.count, (sqlite::cache_slots * sqlite::default_connections) + sqlite::slots_reserve);
    CHECK_EQ(rig->pages.size(), budget * sqlite::default_connections);
    CHECK_EQ(rig->engine->heap().budget() + (s.count * s.size), rig->pages.size());  // the heap before the slots
    CHECK(s.size > sqlite::page_size && s.size < 2 * sqlite::page_size && s.size % 8 == 0);
    CHECK_EQ(slots_a_page, 100U);
    CHECK(heap_a_page < 64);
    CHECK_EQ(one.slots.peak, one.slots.used);  // the marks from the fresh engine heap on
    CHECK_EQ(s.overflow_peak, 0U);
    CHECK(s.used <= sqlite::cache_slots);
    CHECK(cache <= sqlite::cache_slots * s.size);
    CHECK(all.top() <= budget);
    if (slots_pinned) {
        CHECK_EQ(s.size, slot_measured);
        CHECK_EQ(s.used, sqlite::cache_slots);  // a full cache, as cache_slots holds it
    }
}

QIVEN_CASE(storage_sqlite, cache_shared) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = plain(*rig);
    for (const std::size_t connections : {sqlite::default_connections, std::size_t{1}}) {
        rig->fresh_engine(connections);
        sqlite::Connection a = opened(path);
        sqlite::Connection b = connected(path);
        CHECK_EQ(a.integer(all_rows, test_at), all_bytes);
        const Held first = held(*rig);
        b.exec(cache_setting, test_at);  // b's schema read once a's cache is counted
        CHECK_EQ(b.integer(all_rows, test_at), all_bytes);
        CHECK_EQ(a.integer(all_rows, test_at), all_bytes);
        const Held both = held(*rig);
        const sqlite::Slots& s = both.slots;
        std::cout << "storage_sqlite.cache_shared: " << connections << " connections' engine heap (" << s.count
                  << " slots, heap " << rig->engine->heap().budget() << "), two caches full at once: first "
                  << first.slots.used << " slots, then " << s.used << " (mark " << s.peak << "), heap " << both.used
                  << " (peak " << both.peak << "), past the slots " << s.overflow << " (mark " << s.overflow_peak
                  << "): peak " << both.top() << " against " << budget * connections << " for its connections, "
                  << budget * 2 << " for two\n";
        CHECK(both.top() <= budget * 2);
        // a's full cache and b's first page buffer, the engine's reserve
        // beside the slots letting a's cache fill
        CHECK(!slots_pinned || first.slots.used == sqlite::cache_slots + 1);
        if (connections >= 2) {
            CHECK(!slots_pinned || s.used == 2 * sqlite::cache_slots);
            CHECK_EQ(s.overflow_peak, 0U);
        } else {
            // Past its last free slots the engine recycles a cache's pages;
            // pages it cannot recycle, a temporary table's in memory
            // (temp_store 2, which the case sets here), it takes from the
            // heap, a class of 8,192 each, within the heap's budget.
            CHECK(s.peak <= s.count);
            a.exec("PRAGMA temp_store = 2", test_at);
            a.exec("CREATE TEMP TABLE m AS SELECT x FROM t WHERE rowid <= 60", test_at);
            const Held past = held(*rig);
            std::cout << "storage_sqlite.cache_shared: then a temporary table of 60 pages: " << past.slots.used
                      << " slots, past the slots " << past.slots.overflow << " (mark " << past.slots.overflow_peak
                      << "), heap " << past.used << " (peak " << past.peak << ") of " << rig->engine->heap().budget()
                      << "\n";
            CHECK(past.slots.overflow_peak >= past.slots.overflow);
            CHECK(past.slots.overflow >= 50 * 2 * sqlite::page_size);
            CHECK(past.peak <= rig->engine->heap().budget());
        }
    }
}

// FTS5, which the engine's build compiles in: on a plain database, an fts5
// table made, two rows inserted, a MATCH query finding the one that holds
// its word.
QIVEN_CASE(storage_sqlite, fts5) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = text(rig->root / "fts5.db");
    sqlite::Connection c = made(path);
    c.exec("CREATE VIRTUAL TABLE words USING fts5 (body)", test_at);
    c.exec("INSERT INTO words (rowid, body) VALUES (1, 'a pinned engine'), (2, 'another row')", test_at);
    CHECK_EQ(c.integer("SELECT count(*) FROM words WHERE words MATCH 'pinned'", test_at), 1);
    CHECK_EQ(c.integer("SELECT rowid FROM words WHERE words MATCH 'pinned'", test_at), 1);
}

QIVEN_CASE(storage_sqlite, slots_refused) {
    const auto rig = std::make_unique<Rig>();
    const std::string path = plain(*rig);
    const Held before = held(*rig);
    // A second engine heap while one is installed, on pages of the case's
    // own that could hold its slots: refused by the heap's own check, the
    // installed one's bytes and slots as before.
    {
        std::vector<std::byte> pages(budget);
        refused_as(qiven_ctx, raised([&] { const sqlite::EngineHeap second(pages, 1); }), sqlite::Refusal::installed,
                   0, "an engine heap is installed already");
        CHECK(sqlite::engine_ready());
        const Held after = held(*rig);
        CHECK(after.used == before.used && after.peak == before.peak);
        CHECK(after.slots.count == before.slots.count && after.slots.size == before.slots.size);
        CHECK(after.slots.used == before.slots.used && after.slots.overflow == before.slots.overflow);
    }
    {
        sqlite::Connection c = opened(path);
        CHECK_EQ(c.integer(all_rows, test_at), all_bytes);
        const sqlite::Slots after = rig->engine->slots();
        CHECK_EQ(after.count, before.slots.count);
        CHECK_EQ(after.size, before.slots.size);
        CHECK(after.used > sqlite::cache_slots / 2);
    }
    rig->engine.reset();
    CHECK(!sqlite::engine_ready());
    // Pages short of the slots: refused, nothing installed.
    {
        std::vector<std::byte> short_pages(budget / 2);
        refused_as(qiven_ctx, raised([&] { const sqlite::EngineHeap h(short_pages, 1); }), sqlite::Refusal::slots, 0,
                   "the engine heap's pages cannot hold the page cache's slots");
        CHECK(!sqlite::engine_ready());
    }
#if defined(QIVEN_FAULTS)
    // The slots' configuration failed: the engine shut down, none kept.
    qiven::memory::Countdown calls;
    calls.arm(1);
    sqlite::attach_calls(&calls);
    const std::optional<sqlite::EngineError> configured =
        raised([&] { const sqlite::EngineHeap fourth(rig->pages, sqlite::default_connections); });
    sqlite::attach_calls(nullptr);
    std::cout << "storage_sqlite.slots_refused: configuration: "
              << (configured ? configured->detail() : std::string_view("none")) << ", code "
              << (configured ? configured->code() : 0) << "\n";
    CHECK(calls.fired());
    refused_as(qiven_ctx, configured, sqlite::Refusal::configuration, io_error, "the engine refused its configuration");
    CHECK(!sqlite::engine_ready());
#endif
    rig->fresh_engine();
    sqlite::Connection c = opened(path);
    CHECK_EQ(c.integer(all_rows, test_at), all_bytes);
    CHECK_EQ(rig->engine->slots().count, before.slots.count);
    CHECK(rig->engine->slots().used > sqlite::cache_slots / 2);
}

// Calls refused at the site their caller names, each EngineError with its
// refusal, its code, its detail and that site: an open with no engine heap
// (no_heap); a value a unique column holds already, the engine's step
// failing with its extended code (engine); an empty statement (empty); a
// query with no row (no_row), at a site past each field's bytes: an
// operation of 40 bytes held as its first 32, and a path whose character
// of three bytes crosses its 512th held up to that character, no part of
// it kept.
QIVEN_CASE(storage_sqlite, refused_at) {
    static_assert(sqlite::operation_bytes == 32 && sqlite::path_bytes == 512);
    const auto rig = std::make_unique<Rig>();
    const std::string path = text(rig->root / "refused.db");
    std::ofstream(path, std::ios::binary).flush();  // 0 bytes: an empty database
    rig->engine.reset();
    {
        std::vector<std::byte> bytes(4096);
        Arena arena(bytes);
        const String name(path, Chars(arena));
        const sqlite::Site opening{"open", "refused.db"};
        refused_as(qiven_ctx, raised([&] { (void)sqlite::Connection::open(name, false, no_wait, opening); }),
                   sqlite::Refusal::no_heap, 0, "no engine heap installed", opening);
    }
    rig->fresh_engine();
    sqlite::Connection c = made(path);
    c.exec("CREATE TABLE u (x INTEGER UNIQUE)", test_at);
    c.exec("INSERT INTO u VALUES (1)", test_at);
    const sqlite::Site inserting{"insert", "u"};
    refused_as(qiven_ctx, raised([&] { c.exec("INSERT INTO u VALUES (1)", inserting); }), sqlite::Refusal::engine,
               unique_held, "", inserting);
    const sqlite::Site preparing{"prepare", "u"};
    refused_as(qiven_ctx, raised([&] { (void)c.prepare("", preparing); }), sqlite::Refusal::empty, 0,
               "an empty statement", preparing);
    // The euro sign, three bytes, over the path's 511th to 513th.
    const std::string operation(sqlite::operation_bytes + 8, 'o');
    const std::string long_path = std::string(sqlite::path_bytes - 2, 'p') + "\xE2\x82\xAC" + "q";
    const sqlite::Site cut{std::string_view(operation).substr(0, sqlite::operation_bytes),
                           std::string_view(long_path).substr(0, sqlite::path_bytes - 2)};
    refused_as(qiven_ctx, raised([&] { (void)c.integer("SELECT 1 WHERE 0", {operation, long_path}); }),
               sqlite::Refusal::no_row, 0, "a query with no row", cut);
    CHECK_EQ(c.integer("SELECT count(*) FROM u", test_at), 1);
}
