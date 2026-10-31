// store/sqlite's states, on the fake, over a scratch directory of the
// case's own, the rig of sqlite_rig.hpp. second_engine: an engine made
// while one is installed refused before its pages, the first serving on.
// pending: a forget's purge left pending by a reader's snapshot ("reader"),
// heads reporting it; a writer holding the write lock, an append StoreBusy
// after the busy wait and a purge pending "writer"; done once both end;
// scans by a scope, a lane and an after past the end. forged: a store's
// file altered as no store would, each refused at open by coherence's
// check, its kind and detail, then the file as written open again. The
// writing cases over plain records, each write verified and reopened.

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/stores/records.hpp"
#include "support/stores/sqlite_rig.hpp"

using namespace qiven3::test::sqlite_rig;  // NOLINT(google-build-using-namespace)

namespace {

using qiven3::memory::Vector;
using qiven3::store::EntryView;
using qiven3::store::GuardView;
using qiven3::test::records::chain;
using qiven3::test::records::id_of;
using qiven3::test::records::Rec;
using qiven3::test::records::views;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::RangeView;
using qiven3::store::Written;
using Store = qiven3::store::SQLiteStore<Fake>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A store holding A, B, D and ACC on the implementer's lane, plain
// records, the purge done; its records kept for the cases' later calls,
// E a forget of B.
struct Course {
    Course()
        : lane(chain(qiven_scope, implementer_lane, {"A", "B", "D", "ACC"})), first{lane[0], lane[1], lane[2]},
          acc{lane[3]}, e{qiven3::test::records::forget("E", qiven_scope, implementer_lane, 5, "ACC", "B")},
          again{lane[0]}, first_lines(views(first)), acc_lines(views(acc)),
          e_lines(views(e)), again_lines(views(again)), acc_id(id_of(acc[0])), e_id(id_of(e[0])),
          guard{GuardView{qiven_scope, implementer_lane, acc_id}} {}
    void fill(Store& store) const {
        (void)store.append(qiven_scope, implementer_lane, std::nullopt, first_lines);
        (void)store.append(qiven_scope, implementer_lane, id_of(first[2]), acc_lines);
    }
    std::vector<Rec> lane;
    std::vector<Rec> first;
    std::vector<Rec> acc;
    std::vector<Rec> e;
    std::vector<Rec> again;
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
    qiven3::store::Record h(chars);
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
     "StoreCorrupt incoherent: body: the rows not the entries with a body key no forget targets"},
    {"DELETE FROM meta WHERE name = 'count'", "StoreCorrupt incoherent: meta: a row missing"},
    {"INSERT INTO meta (name, value) VALUES ('other', 0)", "StoreCorrupt incoherent: meta: a row of another name"},
}};

}  // namespace

QIVEN3_CASE(store_sqlite, pending) {
    const auto rig = std::make_unique<Rig>();
    const auto course = std::make_unique<Course>();
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
    const qiven3::store::Counts counts{5, 3, 1, 1};
    CHECK(store->verify() == counts);
    CHECK(Store::open(path, false, true, rig->fake).verify() == counts);
}

QIVEN3_CASE(store_sqlite, forged) {
    const auto rig = std::make_unique<Rig>();
    const auto course = std::make_unique<Course>();
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
    Store store = Store::open(path, false, false, rig->fake);  // the base again, as written
    CHECK(store.verify() == (qiven3::store::Counts{5, 3, 1, 1}));
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
    const auto course = std::make_unique<Course>();
    const std::string path = text(rig->root / "e" / "store.db");
    const auto store = std::make_unique<Store>(Store::open(path, true, false, rig->fake));
    course->fill(*store);
    const Held before = held(*rig);
    const std::size_t calls = rig->fake.calls().size();
    const std::size_t lent = rig->fake.lent();
    const std::string installed = "StoreError internal: an engine heap is installed already";
#if defined(QIVEN_FAULTS)
    // The platform's calls, the engine's and the installed heap's allocations counted, none failed.
    qiven3::memory::Countdown platform;
    qiven3::memory::Countdown engine;
    qiven3::memory::Countdown heap;
    rig->fake.attach(&platform);
    sqlite::attach_calls(&engine, nullptr);
    rig->engine->heap().attach(&heap);
#endif
    const std::string refused = raised([&] { const Engine second(rig->fake); });
#if defined(QIVEN_FAULTS)
    rig->fake.attach(nullptr);
    sqlite::attach_calls(nullptr, nullptr);
    rig->engine->heap().attach(nullptr);
    std::cout << "store_sqlite.second_engine: faults: platform calls " << platform.calls() << ", engine calls "
              << engine.calls() << ", engine heap allocations " << heap.calls() << "\n";
    CHECK(platform.calls() == 0 && engine.calls() == 0 && heap.calls() == 0);
#else
    std::cout << "store_sqlite.second_engine: faults: not applicable: built without QIVEN_FAULTS\n";
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
    CHECK(Store::open(path, false, true, rig->fake).verify() == counts);
}
