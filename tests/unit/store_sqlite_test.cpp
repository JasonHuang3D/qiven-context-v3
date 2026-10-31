// store/sqlite's smoke case, on the fake, over a scratch directory of the
// case's own: the store type meets the contract's concept and declares
// every capability; SETTINGS as the text's table; create makes the
// directory and the file, the schema, the meta rows and the identity, its
// -wal 0 bytes; each setting read back; a second create StoreError exists;
// open again, and read-only; verify, an empty store's counts all 0;
// close, then closed; missing, a 0-byte file, a foreign file left
// byte-equal, another version, a store's of version 1 left byte-equal, a
// file that is no database; the guard's
// checkout (a .git directory, then a file) and synced folder, by open and
// alone; incoherent meta; create read-only and invalid limits refused; a
// second engine heap refused while one is installed. store_sqlite.writes,
// over plain records: append, a HeadConflict, size and its figures;
// forget's kind, guard and erasure; each purge done, meta's vacuumed and
// purged at forgets; verify, and a read-only reopen's, its purge refused,
// its close attempting none; close, then closed. store_sqlite.reads, over
// plain records: head, heads by scope set, header, body and forgotten,
// NotFound, scans by range, verify and a reopen's; a body row deleted, the
// scan StoreCorrupt incoherent; a scan left open past close, StoreError
// closed.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/stores/records.hpp"

namespace {

namespace fs = std::filesystem;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;
using qiven3::store::Capabilities;
using qiven3::store::Engine;
using qiven3::store::EntryView;
using qiven3::test::records::added;
using qiven3::test::records::chain;
using qiven3::test::records::forget;
using qiven3::test::records::id_of;
using qiven3::test::records::Rec;
using qiven3::test::records::views;
using Arena = qiven3::memory::Arena;
using Chars = qiven3::memory::Allocator<char>;
using qiven3::store::GuardView;
using qiven3::store::Id;
using qiven3::store::Vector;
using qiven3::store::Written;
using qiven3::store::Limits;
using qiven3::store::Operation;
using qiven3::store::StoreError;
using qiven3::store::WhereView;
using Store = qiven3::store::SQLiteStore<FakePlatform>;
namespace sqlite = qiven3::store::sqlite;

static_assert(qiven3::store::Store<Store>, "the SQLite store meets the contract");
static_assert(Store::capabilities == Capabilities{true, true, true}, "the SQLite store declares every capability");

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end; a folder
// under it the fake's synced names.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven3-sqlite-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    [[nodiscard]] Config config() const {
        Config config;
        config.root = text(root);
        config.seed = 7;
        config.synced.emplace_back(text(root / "cloud"), "OneDrive");
        return config;
    }
    fs::path root;
};

// The scratch, the fake over it and the engine heap, on the heap: the
// fake and its configuration are past a frame's share.
struct Rig {
    Rig() : fake(scratch.config()), engine(fake) {}
    Scratch scratch;
    FakePlatform fake;
    Engine<FakePlatform> engine;
};

// The error a call raised, as "Kind code" (the kind's own code), or "none".
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
        } else if (e.full()) {
            code = qiven3::store::name(*e.full());
        }
        return std::string(qiven3::store::name(e.kind())) + " " + code + (e.detail().empty() ? "" : ": ") +
               std::string(e.detail());
    }
    return "none";
}

// A frame of the function's own: clang inlined smoke's steps, and a store
// for each open, into frames past the bound. open_error: what open raised.
#ifdef __GNUC__
#define QIVEN3_OWN_FRAME __attribute__((noinline))
#else
#define QIVEN3_OWN_FRAME
#endif
QIVEN3_OWN_FRAME std::string open_error(Rig& rig, const fs::path& path, bool create = false, bool read_only = false,
                                        const Limits& limits = Limits{}) {
    return raised([&] { (void)Store::open(text(path), create, read_only, rig.fake, limits); });
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}

constexpr WhereView test_at = sqlite::site("test", "scratch");

// A connection of the case's own through the wrapper, to make or alter a
// file as no store would.
void alter(const fs::path& path, std::string_view sql) {
    std::vector<std::byte> bytes(4096);
    qiven3::memory::Arena arena(bytes);
    const qiven3::memory::String name(text(path), qiven3::memory::Allocator<char>(arena));
    sqlite::Connection c = sqlite::Connection::open(name, false, test_at);
    c.exec(sql, test_at);
}

// SETTINGS, row by row, as the text's table gives them.
void settings_table(qiven3::test::Context& qiven3_ctx) {
    constexpr std::array<std::string_view, 11> names = {
        "journal_mode", "synchronous",  "secure_delete", "temp_store",     "foreign_keys", "trusted_schema",
        "cell_size_check", "auto_vacuum", "busy_timeout", "application_id", "user_version"};
    constexpr std::array<std::string_view, 11> values = {"wal", "2", "1",    "2",          "1", "0",
                                                         "1",   "0", "5000", "1903571713", "2"};
    CHECK_EQ(Store::SETTINGS.size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        CHECK_EQ(Store::SETTINGS.at(i).name.view(), names.at(i));
        CHECK_EQ(Store::SETTINGS.at(i).value.view(), values.at(i));
    }
}

// create, then what it made, read through the store's connection.
QIVEN3_OWN_FRAME void created(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const fs::path path = rig.scratch.root / "a" / "store.db";
    Store store = Store::open(text(path), true, false, rig.fake);
    CHECK(!store.read_only());
    CHECK(store.limits() == Limits{});
    CHECK(fs::is_regular_file(path));
    CHECK(fs::exists(text(path) + "-wal"));
    CHECK_EQ(fs::file_size(text(path) + "-wal"), 0U);
    CHECK_EQ(store.opened().entries, 0);
    CHECK_EQ(store.opened().bodies, 0);
    CHECK(!store.opened().pending());
    CHECK(store.opened_purge().done);
    sqlite::Connection& c = store.connection();
    CHECK_EQ(c.integer("PRAGMA application_id", test_at), sqlite::application_id);
    CHECK_EQ(c.integer("PRAGMA user_version", test_at), sqlite::user_version);
    CHECK(!sqlite::differing(c, test_at).has_value());
    CHECK_EQ(c.integer("SELECT count(*) FROM sqlite_schema WHERE type = 'table'", test_at), 4);
    CHECK_EQ(c.integer("SELECT count(*) FROM sqlite_schema WHERE type = 'index' AND sql IS NOT NULL", test_at), 2);
    CHECK_EQ(c.integer("SELECT count(*) FROM meta WHERE value = 0", test_at), 5);
    CHECK_EQ(c.integer("SELECT count(*) FROM pragma_table_list WHERE schema = 'main' AND strict = 1", test_at), 4);
    CHECK(rig.engine.heap().used() > 0);
    CHECK(rig.engine.heap().peak() <= rig.engine.heap().budget());
    CHECK(store.verify() == qiven3::store::Counts{});

    const std::string before = bytes_of(path);
    CHECK(open_error(rig, path, true).starts_with("StoreError exists"));
    CHECK(bytes_of(path) == before);
    CHECK(store.close().done);
    CHECK(raised([&] { (void)store.close(); }) == "StoreError closed");
    CHECK(raised([&] { (void)store.size(); }) == "StoreError closed");
}

// open again, read-write and read-only.
QIVEN3_OWN_FRAME void reopened(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const std::string path = text(rig.scratch.root / "a" / "store.db");
    Store store = Store::open(path, false, false, rig.fake);
    CHECK(!sqlite::differing(store.connection(), test_at).has_value());
    Store reader = Store::open(path, false, true, rig.fake);
    CHECK(reader.read_only());
    CHECK(!sqlite::differing(reader.connection(), test_at).has_value());
    CHECK(raised([&] { (void)reader.append("project:qiven", "implementer", std::nullopt, {}); }) ==
          "StoreError read_only");
    Store moved = std::move(store);
    CHECK(moved.verify() == qiven3::store::Counts{});
}

// The files open refuses.
QIVEN3_OWN_FRAME void refused(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const fs::path& root = rig.scratch.root;
    CHECK(open_error(rig, root / "none.db") == "StoreError missing");
    write_file(root / "empty.db", "");
    CHECK(open_error(rig, root / "empty.db").starts_with("StoreCorrupt corrupt"));
    write_file(root / "foreign.db", "");
    alter(root / "foreign.db", "CREATE TABLE t (x INTEGER)");
    const std::string foreign = bytes_of(root / "foreign.db");
    CHECK(open_error(rig, root / "foreign.db") == "StoreCorrupt foreign: application_id 0");
    CHECK(bytes_of(root / "foreign.db") == foreign);
    alter(root / "foreign.db", "PRAGMA application_id = 1903571713");
    alter(root / "foreign.db", "PRAGMA user_version = 3");
    CHECK(open_error(rig, root / "foreign.db") == "StoreCorrupt version: user_version 3");
    // A store's file of version 1, before the body key's column: refused,
    // the file unchanged, and none left beside it.
    const fs::path made = root / "a" / "store.db";
    alter(made, "PRAGMA user_version = 1");
    const std::string one = bytes_of(made);
    CHECK(open_error(rig, made) == "StoreCorrupt version: user_version 1");
    CHECK(bytes_of(made) == one && !fs::exists(text(made) + "-wal") && !fs::exists(text(made) + "-shm"));
    alter(made, "PRAGMA user_version = 2");
    write_file(root / "text.db", std::string(1024, 'x'));
    CHECK(open_error(rig, root / "text.db").starts_with("StoreCorrupt not_a_store"));
    CHECK(open_error(rig, root / "ro.db", true, true) == "StoreError read_only: create");
    Limits wrong;
    wrong.max_count = 1;
    CHECK(open_error(rig, root / "l.db", true, false, wrong) == "StoreError settings: limits");
    CHECK(!fs::exists(root / "ro.db") && !fs::exists(root / "l.db"));
}

// The guard: a checkout by a .git directory, then a file, and a synced
// folder, by open and alone; nothing made.
QIVEN3_OWN_FRAME void guarded(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const fs::path& root = rig.scratch.root;
    fs::create_directories(root / "repo" / ".git");
    fs::create_directories(root / "work");
    write_file(root / "work" / ".git", "gitdir: elsewhere\n");
    fs::create_directories(root / "cloud");
    CHECK(open_error(rig, root / "repo" / "sub" / "store.db", true) == "StoreError location: checkout");
    CHECK(!fs::exists(root / "repo" / "sub"));
    CHECK(open_error(rig, root / "work" / "store.db", true) == "StoreError location: checkout");
    CHECK(open_error(rig, root / "cloud" / "store.db", true) == "StoreError location: synced");
    CHECK(!fs::exists(root / "cloud" / "store.db"));
    CHECK(raised([&] { Store::guard(text(root / "repo"), rig.fake); }) == "StoreError location: checkout");
    CHECK(raised([&] { Store::guard(text(root / "cloud" / "deep"), rig.fake); }) == "StoreError location: synced");
    CHECK(raised([&] { Store::guard(text(root / "a"), rig.fake); }) == "none");
}

// Coherence refuses a meta that disagrees with the rows.
QIVEN3_OWN_FRAME void incoherent(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const fs::path path = rig.scratch.root / "a" / "store.db";
    alter(path, "UPDATE meta SET value = 1 WHERE name = 'forgets'");
    CHECK(open_error(rig, path).starts_with("StoreCorrupt incoherent: meta: forgets"));
    alter(path, "UPDATE meta SET value = 0 WHERE name = 'forgets'");
    alter(path, "INSERT INTO meta (name, value) VALUES ('other', 0)");
    CHECK(open_error(rig, path).starts_with("StoreCorrupt incoherent: meta: a row of another name"));
    alter(path, "DELETE FROM meta WHERE name = 'other'");
    {
        Store store = Store::open(text(path), false, false, rig.fake);
        store.connection().exec("UPDATE meta SET value = value WHERE name = 'none'", test_at);
        CHECK_EQ(store.connection().changes(), 0);
        store.connection().exec("UPDATE meta SET value = value WHERE name = 'size'", test_at);
        CHECK_EQ(store.connection().changes(), 1);
    }
    CHECK(open_error(rig, path) == "none");
}

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer = "implementer";
constexpr std::string_view reviewer = "reviewer";

void writes(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const std::vector<Rec> lane = chain(qiven_scope, implementer, {"A", "B", "D", "ACC"});
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    Arena arena(pages);
    Vector<Id> erased{Chars(arena)};
    const std::string path = text(rig.scratch.root / "w" / "store.db");
    const auto owned = std::make_unique<Store>(Store::open(path, true, false, rig.fake));  // past a frame's share
    Store& store = *owned;
    const std::vector<Rec> first = {lane[0], lane[1]};
    const std::vector<EntryView> first_lines = views(first);
    const Written w = store.append(qiven_scope, implementer, std::nullopt, first_lines);
    CHECK_EQ(w.head.id.view(), id_of(first[1]));
    CHECK_EQ(w.head.seq, 2);
    CHECK_EQ(w.volume.count, 2U);
    CHECK_EQ(w.volume.size, added(first));
    CHECK(w.purge.done);
    CHECK(store.peak() > 0 && store.peak() <= *qiven3::store::operation_budget(*qiven3::store::given(first_lines)));
    const std::vector<Rec> later = {lane[2]};
    const std::vector<EntryView> later_lines = views(later);
    CHECK(raised([&] { (void)store.append(qiven_scope, implementer, std::nullopt, later_lines); })
              .starts_with("HeadConflict"));
    CHECK_EQ(store.append(qiven_scope, implementer, id_of(first[1]), later_lines).head.seq, 3);
    const std::vector<Rec> acc = {lane[3]};
    const std::vector<EntryView> acc_lines = views(acc);
    (void)store.append(qiven_scope, implementer, id_of(later[0]), acc_lines);
    const auto sizes = std::make_unique<const qiven3::store::Sizes>(store.size());  // past a frame's share
    CHECK_EQ(sizes->volume.count, 4U);
    CHECK_EQ(sizes->volume.size, added(first) + added(later) + added(acc));
    REQUIRE(sizes->figures == 6U);
    CHECK_EQ(sizes->view()[0].name.view(), std::string_view("page_size"));
    CHECK(sizes->view()[0].value >= 512U && sizes->view()[3].value > 0U);

    const std::vector<Rec> wrong = {lane[0]};
    const std::vector<Rec> call = {forget("E", qiven_scope, implementer, 5, "ACC", "B")};
    const std::vector<EntryView> wrong_lines = views(wrong);
    const std::vector<EntryView> call_lines = views(call);
    const std::string head = id_of(acc[0]);
    const std::vector<GuardView> guard = {GuardView{qiven_scope, implementer, head}};
    CHECK(raised([&] { (void)store.forget(qiven_scope, implementer, head, wrong_lines, guard, erased); })
              .starts_with("StoreError forget_kind"));
    CHECK(raised([&] { (void)store.forget(qiven_scope, implementer, head, call_lines, {}, erased); })
              .starts_with("HeadConflict"));
    const Written f = store.forget(qiven_scope, implementer, head, call_lines, guard, erased);
    CHECK_EQ(f.head.id.view(), id_of(call[0]));
    CHECK_EQ(f.volume.count, 5U);
    CHECK_EQ(f.volume.size, sizes->volume.size - (first[1].record->size() + 1U) + added({call[0]}));
    REQUIRE(erased.size() == 1U);
    CHECK_EQ(erased[0].view(), id_of(first[1]));
    CHECK(f.purge.done);
    sqlite::Connection& c = store.connection();
    CHECK_EQ(c.integer("SELECT count(*) FROM meta WHERE name IN ('forgets', 'vacuumed', 'purged') AND value = 1",
                       test_at),
             3);
    CHECK_EQ(c.integer("SELECT count(*) FROM body WHERE id = '" + id_of(first[1]) + "'", test_at), 0);
    CHECK(store.purge().done);
    CHECK_EQ(store.size().volume.size, f.volume.size);
    const qiven3::store::Counts counts{5, 3, 1, 1};
    CHECK(store.verify() == counts);
    {
        const auto reader = std::make_unique<Store>(Store::open(path, false, true, rig.fake));
        CHECK_EQ(raised([&] { (void)reader->purge(); }), std::string("StoreError read_only"));
        CHECK(reader->verify() == counts);
        CHECK_EQ(reader->size().volume.count, 5U);
        CHECK(reader->close().done);
    }
    CHECK(store.close().done);
    CHECK_EQ(raised([&] { (void)store.purge(); }), std::string("StoreError closed"));
}

// Each scanned record's id; cut, the same of records given.
std::string scanned(Store& store, const Chars& chars, const qiven3::store::RangeView& range) {
    std::string ids;
    qiven3::store::Record r(chars);
    for (auto scan = store.scan(range); scan.next(r);) {
        ids += std::string(r.id.view()) + " ";
    }
    return ids;
}
std::string cut(const std::vector<Rec>& lines) {
    std::string ids;
    for (const Rec& s : lines) {
        ids += id_of(s) + " ";
    }
    return ids;
}

// Two lanes, reviewer's M2 and M1 forgotten; each read; then a body row
// deleted under the store.
void reads(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    std::vector<std::byte> pages(std::size_t{1} << 17U);
    Arena arena(pages);
    const Chars chars(arena);
    Vector<Id> erased(chars);
    const std::string path = text(rig.scratch.root / "r" / "store.db");
    const auto store = std::make_unique<Store>(Store::open(path, true, false, rig.fake));
    const std::vector<Rec> mine = chain(qiven_scope, implementer, {"A", "B"});
    const std::vector<Rec> theirs = chain(qiven_scope, reviewer, {"C", "M1", "M2"});
    (void)store->append(qiven_scope, implementer, std::nullopt, views(mine));
    (void)store->append(qiven_scope, reviewer, std::nullopt, views(theirs));
    const auto f2 = std::make_unique<const Rec>(forget("X2", qiven_scope, reviewer, 4, "M2", "M2"));
    const auto f3 = std::make_unique<const Rec>(forget("X3", qiven_scope, reviewer, 5, "X2", "M1"));
    const std::string a = id_of(mine[0]);
    const std::string b = id_of(mine[1]);
    const std::string m2 = id_of(theirs[2]);
    const std::vector<GuardView> guard = {GuardView{qiven_scope, implementer, b}, GuardView{qiven_scope, reviewer, m2}};
    const std::vector<Rec> forgets = {*f2, *f3};
    (void)store->forget(qiven_scope, reviewer, m2, views(forgets), guard, erased);
    const qiven3::store::Counts counts{7, 3, 2, 2};
    CHECK(store->verify() == counts);
    {
        const auto reader = std::make_unique<Store>(Store::open(path, false, true, rig.fake));
        CHECK(reader->verify() == counts);
    }
    CHECK(store->head(qiven_scope, implementer).value().seq == 2 && !store->head(qiven_scope, "nobody"));
    Vector<qiven3::store::LaneHead> rows(chars);
    const auto all = std::make_unique<const qiven3::store::Heads>(store->heads(std::nullopt, rows));
    REQUIRE(rows.size() == 2U);
    CHECK(rows[1].lane.view() == reviewer && rows[1].head.seq == 5);
    CHECK(all->forgets == 2U && all->purge.done);
    const std::array<std::string_view, 2> sets = {"person", "project:*"};
    CHECK(store->heads(std::span(sets).first(1), rows).forgets == 0U && rows.empty());
    CHECK(store->heads(std::span(sets).last(1), rows).forgets == 2U && rows.size() == 2U);

    const auto out = std::make_unique<qiven3::store::Record>(chars);  // past a frame's share
    qiven3::memory::String line(chars);
    store->header(a, line);
    CHECK_EQ(std::string_view(line), mine[0].line);
    std::string lent;  // the same lent from the store's own pages
    store->header(a, [&lent](std::string_view l, const sqlite::Chars&) { lent = l; });
    CHECK_EQ(lent, mine[0].line);
    CHECK(store->peak() > 0 && store->peak() <= qiven3::store::operation_base);
    qiven3::memory::String record(chars);
    CHECK(store->body(a, record) && std::string_view(record) == *mine[0].record);
    CHECK(!store->body(id_of(theirs[1]), record) && !store->body(id_of(*f2), record));
    CHECK(store->forgotten(id_of(theirs[1])) && !store->forgotten(a));
    const std::string none = "sha256:" + std::string(64, '0');
    CHECK(raised([&] { store->header(none, line); }).starts_with("NotFound"));
    CHECK(raised([&] { (void)store->body(none, record); }).starts_with("NotFound"));
    CHECK(raised([&] { (void)store->forgotten(none); }).starts_with("NotFound"));

    CHECK_EQ(scanned(*store, chars, {}), cut(mine) + cut(theirs) + cut(forgets));
    CHECK_EQ(scanned(*store, chars, {qiven_scope, reviewer, 2}), cut({theirs[2], *f2, *f3}));
    CHECK_EQ(scanned(*store, chars, {std::nullopt, std::nullopt, 4}), cut({*f3}));
    CHECK_EQ(scanned(*store, chars, {"person", std::nullopt, std::nullopt}), std::string());

    store->connection().exec("DELETE FROM body WHERE id = '" + a + "'", test_at);
    CHECK(raised([&] { (void)scanned(*store, chars, {}); }).starts_with("StoreCorrupt incoherent"));
    const auto left = std::make_unique<decltype(store->scan({}))>(store->scan({}));
    (void)store->close();
    CHECK_EQ(raised([&] { (void)left->next(*out); }), std::string("StoreError closed"));
}

}  // namespace

QIVEN3_CASE(store_sqlite, reads) {
    const auto rig = std::make_unique<Rig>();
    reads(qiven3_ctx, *rig);
}

QIVEN3_CASE(store_sqlite, writes) {
    const auto rig = std::make_unique<Rig>();
    writes(qiven3_ctx, *rig);
}

QIVEN3_CASE(store_sqlite, smoke) {
    settings_table(qiven3_ctx);
    const auto rig = std::make_unique<Rig>();
    created(qiven3_ctx, *rig);
    reopened(qiven3_ctx, *rig);
    refused(qiven3_ctx, *rig);
    guarded(qiven3_ctx, *rig);
    incoherent(qiven3_ctx, *rig);
    CHECK(raised([&] { const Engine<FakePlatform> second(rig->fake); }) ==
          "StoreError internal: an engine heap is installed already");
    CHECK(sqlite::engine_version() >= sqlite::least_version);
}
