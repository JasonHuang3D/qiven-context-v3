// store/sqlite's states, on the fake, over a scratch directory of the
// case's own. cache: a connection's page cache on the engine's slots, on a
// fresh engine heap for each read of a plain database of 1,500 rows of
// 3,000 bytes, a row a page: a slot each cached page, none of the heap's,
// the full cache's bytes and the connection's peak within the engine's
// budget for a connection; cache_shared: two connections' caches at once,
// within the slots and past them; slots_refused: an engine heap refused
// while one is installed, its slots kept, or by its configuration's
// failed call, none kept; second_engine: an engine made while one is
// installed refused before its pages, the first serving on. pending: a
// forget's purge left pending by a reader's snapshot ("reader"), heads
// reporting it; a writer holding the write lock, an append StoreBusy after
// the busy wait and a purge pending "writer"; done once both end; scans by
// a scope, a lane and an after past the end. forged: a store's file altered
// as no store would, each refused at open by coherence's check, its kind
// and detail.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "checked/stores.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "memory/pool.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::store::GuardView;
using qiven3::store::Header;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::RangeView;
using qiven3::store::WhereView;
using qiven3::store::Written;
using Store = qiven3::checked::SQLiteStore<Fake>;
using Engine = qiven3::store::Engine<Fake>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr WhereView test_at = sqlite::site("test", "scratch");

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The scratch, the fake over it and the engine heap, made again at will;
// on the heap.
struct Rig {
    Rig() : root(fresh_root()), fake(config(root)), engine(std::make_unique<Engine>(fake)) {}
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
        fs::path out = fs::temp_directory_path() / ("qiven3-sqlite-s-" + std::to_string(draw()) +
                                                    std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven3::platform::fake::Config config(const fs::path& root) {
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }
    // A fresh engine heap: every connection on the last closed first.
    void fresh_engine(std::size_t connections = sqlite::default_connections) {
        engine.reset();
        engine = std::make_unique<Engine>(fake, connections);
    }

    fs::path root;
    Fake fake;
    std::unique_ptr<Engine> engine;
};

// A connection of the case's own through the wrapper, as no store opens.
sqlite::Connection raw(const std::string& path) {
    std::vector<std::byte> bytes(4096);
    Arena arena(bytes);
    const String name(path, Chars(arena));
    return sqlite::Connection::open(name, false, test_at);
}

// The error a call raised, as "Kind code: detail", or "none".
template <class F>
std::string raised(const F& f) {
    try {
        f();
    } catch (const StoreError& e) {
        std::string code;
        if (e.code()) {
            code = qiven3::store::name(*e.code());
        } else if (e.corrupt()) {
            code = qiven3::store::name(*e.corrupt());
        } else if (e.cause()) {
            code = qiven3::store::name(*e.cause());
        }
        return std::string(qiven3::store::name(e.kind())) + " " + code + ": " + std::string(e.detail());
    }
    return "none";
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
Held held(Rig& rig) { return {rig.engine->heap().used(), rig.engine->heap().peak(), rig.engine->slots()}; }

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

namespace {

// A store holding A, B, D and ACC on the implementer's lane, the purge
// done; its lines kept for the cases' later calls.
struct Course {
    explicit Course(Fake& fake)
        : book(fake), first{book.stored("A"), book.stored("B"), book.stored("D")}, acc{book.stored("ACC")},
          e{book.stored("E")}, again{book.stored("A")}, first_lines(views(first)), acc_lines(views(acc)),
          e_lines(views(e)), again_lines(views(again)), acc_id(id_of(acc[0])), e_id(id_of(e[0])),
          guard{GuardView{qiven_scope, implementer_lane, acc_id}} {}
    void fill(Store& store) const {
        (void)store.append(qiven_scope, implementer_lane, std::nullopt, first_lines);
        (void)store.append(qiven_scope, implementer_lane, id_of(first[2]), acc_lines);
    }
    Book book;
    std::vector<Stored> first;
    std::vector<Stored> acc;
    std::vector<Stored> e;
    std::vector<Stored> again;
    std::vector<EntryView> first_lines;
    std::vector<EntryView> acc_lines;
    std::vector<EntryView> e_lines;
    std::vector<EntryView> again_lines;
    std::string acc_id;
    std::string e_id;
    std::vector<GuardView> guard;
};

std::string reason(const qiven3::store::Purge& p) { return p.done ? "done" : std::string(p.reason.view()); }

// A scan's count over range.
std::size_t scanned(Store& store, const RangeView& range, const Chars& chars) {
    auto scan = store.scan(range);
    Header h(chars);
    std::size_t n = 0;
    while (scan.next(h)) {
        ++n;
    }
    return n;
}

// The purge held by a reader's snapshot, then by a writer; an append
// StoreBusy beside the writer.
void held(qiven3::test::Context& qiven3_ctx, Store& store, const Course& course, const std::string& path,
          Fake& fake, const Chars& chars) {
    {
        sqlite::Connection reader = raw(path);
        reader.exec("BEGIN", test_at);
        (void)reader.integer("SELECT count(*) FROM entry", test_at);
        Vector<Id> erased(chars);
        const Written w = store.forget(qiven_scope, implementer_lane, course.acc_id, course.e_lines, course.guard,
                                       erased);
        CHECK_EQ(reason(w.purge), std::string("reader"));
        CHECK_EQ(erased.size(), 1U);
        Vector<LaneHead> rows(chars);
        CHECK_EQ(reason(store.heads(std::nullopt, rows).purge), std::string("reader"));
        Store other = Store::open(path, false, true, fake);
        Vector<LaneHead> others(chars);
        const qiven3::store::Heads hs = other.heads(std::nullopt, others);
        CHECK_EQ(hs.forgets, 1U);
        CHECK_EQ(reason(hs.purge), std::string("writer"));
        (void)other.close();
        reader.exec("ROLLBACK", test_at);
    }
    sqlite::Connection writer = raw(path);
    writer.exec("BEGIN IMMEDIATE", test_at);
    CHECK_EQ(reason(store.purge()), std::string("writer"));
    const auto start = std::chrono::steady_clock::now();
    const std::string busy = raised([&] {
        (void)store.append(qiven_scope, implementer_lane, course.e_id, course.again_lines);
    });
    const auto waited = std::chrono::steady_clock::now() - start;
    std::cout << "store_sqlite.pending: " << busy << " after "
              << std::chrono::duration_cast<std::chrono::milliseconds>(waited).count() << " ms\n";
    CHECK(busy.starts_with("StoreBusy"));
    CHECK(waited >= std::chrono::milliseconds(4500));
    writer.exec("ROLLBACK", test_at);
}

// The base file's forgeries: each statement, then open's refusal.
struct Forgery {
    std::string_view sql;
    std::string_view refused;
};
constexpr std::array<Forgery, 9> forgeries = {{
    {"PRAGMA auto_vacuum = 1; VACUUM", "StoreError settings: auto_vacuum 1"},
    {"UPDATE head SET id = (SELECT id FROM entry WHERE seq = 1), seq = 1",
     "StoreCorrupt incoherent: head: a row not naming its lane's last entry"},
    {"DELETE FROM head", "StoreCorrupt incoherent: head: a lane with entries and no row"},
    {"INSERT INTO body (id, record) SELECT target, x'00' FROM entry WHERE kind = 'forget'",
     "StoreCorrupt forgotten_body: a body of a forget or of its target"},
    {"UPDATE meta SET value = value + 1 WHERE name = 'forgets'",
     "StoreCorrupt incoherent: meta: forgets not the forget rows"},
    {"UPDATE meta SET value = 0 WHERE name = 'vacuumed'",
     "StoreCorrupt incoherent: meta: purged, vacuumed and forgets out of order"},
    {"DELETE FROM body WHERE id = (SELECT id FROM entry WHERE seq = 1)",
     "StoreCorrupt incoherent: body: the rows not the entries less twice the forgets"},
    {"DELETE FROM meta WHERE name = 'count'", "StoreCorrupt incoherent: meta: a row missing"},
    {"INSERT INTO meta (name, value) VALUES ('other', 0)", "StoreCorrupt incoherent: meta: a row of another name"},
}};

// Each of A's columns altered beside its line, the store open: a ruled scan StoreCorrupt incoherent; then put back.
void columns(qiven3::test::Context& qiven3_ctx, Rig& rig, const std::string& path, const std::string& a) {
    Store store = Store::open(path, false, false, rig.fake);
    const std::array<std::string, 3> alters = {"scope = scope || 'x'", "seq = seq + 100", "kind = kind || 'x'"};
    const std::array<std::string, 3> backs = {"scope = substr(scope, 1, length(scope) - 1)", "seq = seq - 100",
                                              "kind = substr(kind, 1, length(kind) - 1)"};
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    Arena arena(pages);
    for (std::size_t i = 0; i < alters.size(); ++i) {
        {
            sqlite::Connection c = raw(path);
            c.exec("UPDATE entry SET " + alters.at(i) + " WHERE id = '" + a + "'", test_at);
        }
        Header h{Chars(arena)};
        CHECK_EQ(raised([&] {
                     for (auto scan = store.scan({}); scan.next(h);) {
                     }
                 }),
                 std::string("StoreCorrupt incoherent: an entry's column differs from its line"));
        {
            sqlite::Connection c = raw(path);
            c.exec("UPDATE entry SET " + backs.at(i) + " WHERE id = '" + a + "'", test_at);
        }
        store.header(a, h);
    }
    (void)store.close();
}

}  // namespace

QIVEN3_CASE(store_sqlite, pending) {
    const auto rig = std::make_unique<Rig>();
    const auto course = std::make_unique<Course>(rig->fake);
    const std::string path = text(rig->root / "p" / "store.db");
    const auto store = std::make_unique<Store>(Store::open(path, true, false, rig->fake));
    course->fill(*store);
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    Arena arena(pages);
    const Chars chars(arena);
    held(qiven3_ctx, *store, *course, path, rig->fake, chars);
    CHECK_EQ(reason(store->purge()), std::string("done"));
    {
        Vector<LaneHead> rows(chars);
        CHECK_EQ(reason(store->heads(std::nullopt, rows).purge), std::string("done"));
    }
    CHECK_EQ(scanned(*store, RangeView{qiven_scope, std::nullopt, std::nullopt}, chars), 5U);
    CHECK_EQ(scanned(*store, RangeView{std::nullopt, implementer_lane, std::nullopt}, chars), 5U);
    CHECK_EQ(scanned(*store, RangeView{qiven_scope, implementer_lane, 3}, chars), 2U);
    CHECK_EQ(scanned(*store, RangeView{std::nullopt, std::nullopt, 99}, chars), 0U);
    CHECK_EQ(scanned(*store, RangeView{"project:other", std::nullopt, std::nullopt}, chars), 0U);
    CHECK_EQ(scanned(*store, RangeView{std::nullopt, "reviewer", std::nullopt}, chars), 0U);
    const std::string overlong(qiven3::store::scope_bytes + 1, 'p');
    CHECK_EQ(scanned(*store, RangeView{overlong, std::nullopt, std::nullopt}, chars), 0U);
    CHECK(raised([&] {
        (void)store->append(qiven_scope, implementer_lane, course->acc_id, course->again_lines);
    }).starts_with("HeadConflict"));
    CHECK_EQ(arena.used(), 0U);
}

QIVEN3_CASE(store_sqlite, forged) {
    const auto rig = std::make_unique<Rig>();
    const auto course = std::make_unique<Course>(rig->fake);
    const std::string path = text(rig->root / "f" / "store.db");
    {
        Store store = Store::open(path, true, false, rig->fake);
        course->fill(store);
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        Vector<Id> erased{Chars(arena)};
        const Written w =
            store.forget(qiven_scope, implementer_lane, course->acc_id, course->e_lines, course->guard, erased);
        CHECK(w.purge.done);
        (void)store.close();
    }
    const std::string base = bytes_of(path);
    for (const Forgery& f : forgeries) {
        std::error_code error;
        fs::remove(path + "-wal", error);
        fs::remove(path + "-shm", error);
        std::ofstream(path, std::ios::binary | std::ios::trunc) << base;
        {
            sqlite::Connection c = raw(path);
            for (std::string_view sql = f.sql; !sql.empty();) {
                const std::size_t cut = sql.find("; ");
                c.exec(sql.substr(0, cut), test_at);
                sql = cut == std::string_view::npos ? std::string_view() : sql.substr(cut + 2);
            }
        }
        CHECK_EQ(raised([&] { (void)Store::open(path, false, false, rig->fake); }), std::string(f.refused));
    }
    std::error_code error;
    fs::remove(path + "-wal", error);
    fs::remove(path + "-shm", error);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << base;
    columns(qiven3_ctx, *rig, path, id_of(course->first[0]));
}

QIVEN3_CASE(store_sqlite, paths) {
    CHECK_EQ(sqlite::directory_of("/store.db"), std::string_view("/"));
    CHECK_EQ(sqlite::directory_of("Q:\\store.db"), std::string_view("Q:\\"));
    CHECK_EQ(sqlite::directory_of("Q:\\a\\store.db"), std::string_view("Q:\\a"));
    CHECK_EQ(sqlite::directory_of("/a/b/store.db"), std::string_view("/a/b"));
    CHECK_EQ(sqlite::directory_of("store.db"), std::string_view());
}

// A second engine made while one is installed: refused, typed, before its
// pages, the platform not called and the installed heap not asked, its
// bytes and slots as before; an engine heap on pages of the case's own
// refused by the heap's own check, as when two are made at once; the store
// on the first then served: a scan, a forget with its purge, verify.
QIVEN3_CASE(store_sqlite, second_engine) {
    const auto rig = std::make_unique<Rig>();
    const auto course = std::make_unique<Course>(rig->fake);
    const auto store = std::make_unique<Store>(Store::open(text(rig->root / "e" / "store.db"), true, false, rig->fake));
    course->fill(*store);
    const Held before = held(*rig);
    const std::size_t calls = rig->fake.calls().size();
    const std::size_t lent = rig->fake.lent();
    const std::string installed = "StoreError internal: an engine heap is installed already";
#if defined(QIVEN3_FAULTS)
    // The platform's calls, the engine's and the installed heap's allocations counted, none failed.
    qiven3::memory::Countdown platform;
    qiven3::memory::Countdown engine;
    qiven3::memory::Countdown heap;
    rig->fake.attach(&platform);
    sqlite::attach_calls(&engine, nullptr);
    rig->engine->heap().attach(&heap);
#endif
    const std::string refused = raised([&] { const Engine second(rig->fake); });
#if defined(QIVEN3_FAULTS)
    rig->fake.attach(nullptr);
    sqlite::attach_calls(nullptr, nullptr);
    rig->engine->heap().attach(nullptr);
    std::cout << "store_sqlite.second_engine: faults: platform calls " << platform.calls() << ", engine calls "
              << engine.calls() << ", engine heap allocations " << heap.calls() << "\n";
    CHECK(platform.calls() == 0 && engine.calls() == 0 && heap.calls() == 0);
#else
    std::cout << "store_sqlite.second_engine: faults: not applicable: built without QIVEN3_FAULTS\n";
#endif
    CHECK_EQ(refused, installed);
    CHECK_EQ(rig->fake.calls().size(), calls);
    CHECK_EQ(rig->fake.lent(), lent);
    {
        std::vector<std::byte> pages(qiven3::store::engine_budget);
        CHECK_EQ(raised([&] { const sqlite::EngineHeap h(pages, 1); }), installed);
    }
    const Held after = held(*rig);
    std::cout << "store_sqlite.second_engine: " << refused << "; platform calls " << rig->fake.calls().size() - calls
              << ", pages lent " << rig->fake.lent() - lent << "; heap " << after.used << " (peak " << after.peak
              << ") of " << rig->engine->heap().budget() << ", slots " << after.slots.used << " of "
              << after.slots.count << "\n";
    CHECK(sqlite::engine_ready());
    CHECK(after.used == before.used && after.peak == before.peak);
    CHECK(after.slots.count == before.slots.count && after.slots.size == before.slots.size);
    CHECK(after.slots.used == before.slots.used && after.slots.overflow == before.slots.overflow);
    std::vector<std::byte> bytes(std::size_t{1} << 16U);
    Arena arena(bytes);
    const Chars chars(arena);
    CHECK_EQ(scanned(*store, RangeView{qiven_scope, implementer_lane, std::nullopt}, chars), 4U);
    {
        Vector<Id> erased{chars};
        const Written w =
            store->forget(qiven_scope, implementer_lane, course->acc_id, course->e_lines, course->guard, erased);
        CHECK(w.purge.done);
    }
    const qiven3::store::Counts counts = store->verify();
    CHECK(counts.entries == 5 && counts.forgets == 1 && counts.lanes == 1);
    CHECK_EQ(reason(store->close()), std::string("done"));
    CHECK_EQ(arena.used(), 0U);
}
