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
// second engine heap refused while one is installed.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

namespace {

namespace fs = std::filesystem;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;
using qiven3::store::Capabilities;
using qiven3::store::Engine;
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
    CHECK(raised([&] { (void)store.append("project:qiven", "implementer", std::nullopt, {}); })
              .starts_with("StoreError internal: not built"));

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
    CHECK(raised([&] { (void)moved.size(); }).starts_with("StoreError internal"));
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

}  // namespace

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
