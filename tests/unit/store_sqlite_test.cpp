// store/sqlite's smoke case, on the fake, over a scratch directory of the
// case's own: the store type meets the contract's concept and declares
// every capability; SETTINGS as the text's table; create makes the
// directory and the file, the schema, the meta rows and the identity, its
// -wal 0 bytes; each setting read back; a second create StoreError exists;
// open again, and read-only; the operations not built refuse after the
// gate; close, then closed; missing, a 0-byte file, a foreign file left
// byte-equal, another version, a file that is no database; the guard's
// checkout (a .git directory, then a file) and synced folder, by open and
// alone; incoherent meta; create read-only and invalid limits refused; a
// second engine heap refused while one is installed. store_sqlite.writes:
// append, a content hit refused, a HeadConflict, size and its figures;
// forget's kind, guard, erasure and whole records; each purge done, meta's
// vacuumed and purged at forgets; a read-only object's purge refused, its
// close attempting none; close, then closed. store_sqlite.cleared_many:
// F138's clearing over 4 and over 16 direction records, append's counted
// peak equal, within its budget.

#include <array>
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
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/sqlite.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

namespace fs = std::filesystem;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;
using qiven3::store::Capabilities;
using qiven3::store::Engine;
using qiven3::store::EntryView;
using qiven3::test::stage_book::added;
using qiven3::test::stage_book::Book;
using qiven3::test::stage_book::content_of;
using qiven3::test::stage_book::id_of;
using qiven3::test::stage_book::Stored;
using qiven3::test::stage_book::views;
using Arena = qiven3::memory::Arena;
using Chars = qiven3::memory::Allocator<char>;
namespace header = qiven3::entry::header;
using Slot = header::Slot;
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
                                                         "1",   "0", "5000", "1903571713", "1"};
    CHECK_EQ(Store::SETTINGS.size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        CHECK_EQ(Store::SETTINGS.at(i).name.view(), names.at(i));
        CHECK_EQ(Store::SETTINGS.at(i).value.view(), values.at(i));
    }
}

// create, then what it made, read through the store's connection.
void created(qiven3::test::Context& qiven3_ctx, Rig& rig) {
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
    CHECK(raised([&] { (void)store.verify(); }).starts_with("StoreError internal: not built"));

    const std::string before = bytes_of(path);
    CHECK(raised([&] { (void)Store::open(text(path), true, false, rig.fake); }).starts_with("StoreError exists"));
    CHECK(bytes_of(path) == before);
    CHECK(store.close().done);
    CHECK(raised([&] { (void)store.close(); }) == "StoreError closed");
    CHECK(raised([&] { (void)store.size(); }) == "StoreError closed");
}

// open again, read-write and read-only.
void reopened(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const std::string path = text(rig.scratch.root / "a" / "store.db");
    Store store = Store::open(path, false, false, rig.fake);
    CHECK(!sqlite::differing(store.connection(), test_at).has_value());
    Store reader = Store::open(path, false, true, rig.fake);
    CHECK(reader.read_only());
    CHECK(!sqlite::differing(reader.connection(), test_at).has_value());
    CHECK(raised([&] { (void)reader.append("project:qiven", "implementer", std::nullopt, {}); }) ==
          "StoreError read_only");
    Store moved = std::move(store);
    CHECK(raised([&] { (void)moved.verify(); }).starts_with("StoreError internal"));
}

// The files open refuses.
void refused(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const fs::path& root = rig.scratch.root;
    CHECK(raised([&] { (void)Store::open(text(root / "none.db"), false, false, rig.fake); }) ==
          "StoreError missing");
    write_file(root / "empty.db", "");
    CHECK(raised([&] { (void)Store::open(text(root / "empty.db"), false, false, rig.fake); })
              .starts_with("StoreCorrupt corrupt"));
    write_file(root / "foreign.db", "");
    alter(root / "foreign.db", "CREATE TABLE t (x INTEGER)");
    const std::string foreign = bytes_of(root / "foreign.db");
    CHECK(raised([&] { (void)Store::open(text(root / "foreign.db"), false, false, rig.fake); }) ==
          "StoreCorrupt foreign: application_id 0");
    CHECK(bytes_of(root / "foreign.db") == foreign);
    alter(root / "foreign.db", "PRAGMA application_id = 1903571713");
    alter(root / "foreign.db", "PRAGMA user_version = 2");
    CHECK(raised([&] { (void)Store::open(text(root / "foreign.db"), false, false, rig.fake); }) ==
          "StoreCorrupt version: user_version 2");
    write_file(root / "text.db", std::string(1024, 'x'));
    CHECK(raised([&] { (void)Store::open(text(root / "text.db"), false, false, rig.fake); })
              .starts_with("StoreCorrupt not_a_store"));
    CHECK(raised([&] { (void)Store::open(text(root / "ro.db"), true, true, rig.fake); }) ==
          "StoreError read_only: create");
    Limits wrong;
    wrong.max_count = 1;
    CHECK(raised([&] { (void)Store::open(text(root / "l.db"), true, false, rig.fake, wrong); }) ==
          "StoreError settings: limits");
    CHECK(!fs::exists(root / "ro.db") && !fs::exists(root / "l.db"));
}

// The guard: a checkout by a .git directory, then a file, and a synced
// folder, by open and alone; nothing made.
void guarded(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const fs::path& root = rig.scratch.root;
    fs::create_directories(root / "repo" / ".git");
    fs::create_directories(root / "work");
    write_file(root / "work" / ".git", "gitdir: elsewhere\n");
    fs::create_directories(root / "cloud");
    CHECK(raised([&] { (void)Store::open(text(root / "repo" / "sub" / "store.db"), true, false, rig.fake); }) ==
          "StoreError location: checkout");
    CHECK(!fs::exists(root / "repo" / "sub"));
    CHECK(raised([&] { (void)Store::open(text(root / "work" / "store.db"), true, false, rig.fake); }) ==
          "StoreError location: checkout");
    CHECK(raised([&] { (void)Store::open(text(root / "cloud" / "store.db"), true, false, rig.fake); }) ==
          "StoreError location: synced");
    CHECK(!fs::exists(root / "cloud" / "store.db"));
    CHECK(raised([&] { Store::guard(text(root / "repo"), rig.fake); }) == "StoreError location: checkout");
    CHECK(raised([&] { Store::guard(text(root / "cloud" / "deep"), rig.fake); }) == "StoreError location: synced");
    CHECK(raised([&] { Store::guard(text(root / "a"), rig.fake); }) == "none");
}

// Coherence refuses a meta that disagrees with the rows.
void incoherent(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const fs::path path = rig.scratch.root / "a" / "store.db";
    alter(path, "UPDATE meta SET value = 1 WHERE name = 'forgets'");
    CHECK(raised([&] { (void)Store::open(text(path), false, false, rig.fake); })
              .starts_with("StoreCorrupt incoherent: meta: forgets"));
    alter(path, "UPDATE meta SET value = 0 WHERE name = 'forgets'");
    alter(path, "INSERT INTO meta (name, value) VALUES ('other', 0)");
    CHECK(raised([&] { (void)Store::open(text(path), false, false, rig.fake); })
              .starts_with("StoreCorrupt incoherent: meta: a row of another name"));
    alter(path, "DELETE FROM meta WHERE name = 'other'");
    {
        Store store = Store::open(text(path), false, false, rig.fake);
        store.connection().exec("UPDATE meta SET value = value WHERE name = 'none'", test_at);
        CHECK_EQ(store.connection().changes(), 0);
        store.connection().exec("UPDATE meta SET value = value WHERE name = 'size'", test_at);
        CHECK_EQ(store.connection().changes(), 1);
    }
    CHECK(raised([&] { (void)Store::open(text(path), false, false, rig.fake); }) == "none");
}

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer = "implementer";
constexpr std::string_view reviewer = "reviewer";

// A forget entry like `like`, of target after parent on lane at seq.
Stored forget_of(const Stored& like, const Stored& target, const Stored& parent, std::string_view lane,
                 std::int64_t seq) {
    const header::Header<> e = header::parse_line(like.line);
    const std::string parent_id = id_of(parent);
    const std::string target_id = id_of(target);
    const std::string_view writer = e.value(Slot::writer);
    const std::string role = std::string(lane) + std::string(writer.substr(writer.find('/')));
    header::FieldsView f;
    f.scope = e.value(Slot::scope);
    f.lane = lane;
    f.seq = seq;
    f.parent = parent_id;
    f.kind = "forget";
    f.writer = role;
    f.basis = e.value(Slot::basis);
    f.at = "2026-10-17T13:00:00.000Z";  // after M2's
    f.target = target_id;
    Stored out{std::string(), std::nullopt, std::nullopt};
    header::make(f).line(out.line);
    return out;
}

// A forget of M1 alone refused whole_record, M2 revising it; then M2 and
// M1 forgotten together, both bodies erased.
void records(qiven3::test::Context& qiven3_ctx, Store& store, const Book& book, Vector<Id>& erased) {
    const std::vector<Stored> lane = {book.stored("C"), book.stored("M1"), book.stored("M2")};
    const std::vector<EntryView> held = views(lane);
    (void)store.append(qiven_scope, reviewer, std::nullopt, held);
    const Stored e = book.stored("E");
    const Stored f1 = forget_of(e, lane[1], lane[2], reviewer, 4);
    const Stored f2 = forget_of(e, lane[2], lane[2], reviewer, 4);
    const Stored f3 = forget_of(e, lane[1], f2, reviewer, 5);
    const std::string implementer_head = id_of(e);
    const std::string reviewer_head = id_of(lane[2]);
    const std::vector<GuardView> guard = {GuardView{qiven_scope, implementer, implementer_head},
                                          GuardView{qiven_scope, reviewer, reviewer_head}};
    const std::vector<Stored> part = {f1};
    const std::vector<EntryView> part_lines = views(part);
    CHECK_EQ(raised([&] { (void)store.forget(qiven_scope, reviewer, reviewer_head, part_lines, guard, erased); }),
             std::string("StoreError whole_record: entry 0"));
    CHECK_EQ(store.size().volume.count, 8U);
    const std::vector<Stored> whole = {f2, f3};
    const std::vector<EntryView> whole_lines = views(whole);
    const Written w = store.forget(qiven_scope, reviewer, reviewer_head, whole_lines, guard, erased);
    CHECK_EQ(w.head.seq, 5);
    CHECK_EQ(erased.size(), 2U);
    CHECK(w.purge.done);
}

void writes(qiven3::test::Context& qiven3_ctx, Rig& rig) {
    const Book book(rig.fake);
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    Arena arena(pages);
    Vector<Id> erased{Chars(arena)};
    const std::string path = text(rig.scratch.root / "w" / "store.db");
    Store store = Store::open(path, true, false, rig.fake);
    const std::vector<Stored> hit = {book.with_secret("A", std::nullopt)};
    const std::vector<EntryView> hit_lines = views(hit);
    CHECK(raised([&] { (void)store.append(qiven_scope, implementer, std::nullopt, hit_lines); })
              .starts_with("EntryInvalid"));
    CHECK_EQ(store.size().volume.count, 0U);
    const std::vector<Stored> first = {book.stored("A"), book.stored("B")};
    const std::vector<EntryView> first_lines = views(first);
    const Written w = store.append(qiven_scope, implementer, std::nullopt, first_lines);
    CHECK_EQ(w.head.id.view(), id_of(first[1]));
    CHECK_EQ(w.head.seq, 2);
    CHECK_EQ(w.volume.count, 2U);
    CHECK_EQ(w.volume.size, added(first));
    CHECK(w.purge.done);
    CHECK(store.peak() > 0 && store.peak() <= *qiven3::store::operation_budget(*qiven3::store::given(first_lines)));
    const std::vector<Stored> later = {book.stored("D")};
    const std::vector<EntryView> later_lines = views(later);
    CHECK(raised([&] { (void)store.append(qiven_scope, implementer, std::nullopt, later_lines); })
              .starts_with("HeadConflict"));
    CHECK_EQ(store.append(qiven_scope, implementer, id_of(first[1]), later_lines).head.seq, 3);
    const std::vector<Stored> acc = {book.stored("ACC")};
    const std::vector<EntryView> acc_lines = views(acc);
    (void)store.append(qiven_scope, implementer, id_of(later[0]), acc_lines);
    const qiven3::store::Sizes sizes = store.size();
    CHECK_EQ(sizes.volume.count, 4U);
    CHECK_EQ(sizes.volume.size, added(first) + added(later) + added(acc));
    REQUIRE(sizes.figures == 6U);
    CHECK_EQ(sizes.view()[0].name.view(), std::string_view("page_size"));
    CHECK(sizes.view()[0].value >= 512U && sizes.view()[3].value > 0U);

    const std::vector<Stored> wrong = {book.stored("A")};
    const std::vector<Stored> call = {book.stored("E")};
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
    CHECK_EQ(f.volume.size, sizes.volume.size - (first[1].record->size() + 1U) + call[0].line.size() + 1U);
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
    records(qiven3_ctx, store, book, erased);
    {
        Store reader = Store::open(path, false, true, rig.fake);
        CHECK_EQ(raised([&] { (void)reader.purge(); }), std::string("StoreError read_only"));
        CHECK_EQ(reader.size().volume.count, 10U);
        CHECK(reader.close().done);
    }
    CHECK(store.close().done);
    CHECK_EQ(raised([&] { (void)store.purge(); }), std::string("StoreError closed"));
}

// D's direction record on reviewer's lane, its writer's role the lane's,
// its content clearing content for pattern, at seq after parent (none at
// seq 1), revising target where one is given, at the time given.
Stored clearing(const Book& book, const std::string& content, std::string_view pattern, const Stored* parent,
                const Stored* target, std::int64_t seq, std::string_view at) {
    std::string words = *book.stored("D").content;
    words.insert(words.find("\"episode\":"),
                 R"("clears":{"content":")" + content + R"(","pattern":")" + std::string(pattern) + R"("},)");
    const std::string parent_id = parent == nullptr ? std::string() : id_of(*parent);
    const std::string target_id = target == nullptr ? std::string() : id_of(*target);
    std::string writer;
    return book.made("D", "D", words, [&](header::FieldsView& f) {
        writer = std::string(reviewer) + std::string(f.writer.substr(f.writer.find('/')));
        f.lane = reviewer;
        f.writer = writer;
        f.parent = parent == nullptr ? std::nullopt : std::optional<std::string_view>(parent_id);
        f.target = target == nullptr ? std::nullopt : std::optional<std::string_view>(target_id);
        f.seq = seq;
        f.at = at;
    });
}

// n records on reviewer's lane, n - 1 roots clearing sk-key, then a
// revision of the last root clearing aws-akia: every root folded, the last
// with its revision, before the hit passes; append's counted peak.
std::size_t cleared_peak(qiven3::test::Context& qiven3_ctx, Rig& rig, const Book& book, std::size_t n) {
    const std::vector<Stored> hit = {book.made("A", "A", *book.with_secret("A", std::nullopt).content,
                                               [](header::FieldsView& f) { f.at = "2026-10-17T12:00:00.000Z"; })};
    const std::vector<EntryView> hit_lines = views(hit);
    const std::string content = content_of(hit[0]);
    Store store = Store::open(text(rig.scratch.root / ("c" + std::to_string(n)) / "store.db"), true, false, rig.fake);
    std::vector<Stored> records;
    records.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::string at = "2026-10-17T09:" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ":00.000Z";
        const Stored* parent = records.empty() ? nullptr : &records.back();
        const bool last = i + 1 == n;
        records.push_back(clearing(book, content, last ? "aws-akia" : "sk-key", parent, last ? parent : nullptr,
                                   static_cast<std::int64_t>(i + 1), at));
    }
    const std::vector<EntryView> lines = views(records);
    CHECK_EQ(raised([&] { (void)store.append(qiven_scope, reviewer, std::nullopt, lines); }), std::string("none"));
    CHECK_EQ(raised([&] { (void)store.append(qiven_scope, implementer, std::nullopt, hit_lines); }),
             std::string("none"));
    CHECK(store.peak() <= *qiven3::store::operation_budget(*qiven3::store::given(hit_lines)));
    return store.peak();
}

}  // namespace

QIVEN3_CASE(store_sqlite, cleared_many) {
    const auto rig = std::make_unique<Rig>();
    const Book book(rig->fake);
    const std::size_t four = cleared_peak(qiven3_ctx, *rig, book, 4);
    const std::size_t sixteen = cleared_peak(qiven3_ctx, *rig, book, 16);
    std::cout << "store_sqlite.cleared_many: append " << four << " bytes counted with 4 records, " << sixteen
              << " with 16\n";
    CHECK_EQ(four, sixteen);
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
