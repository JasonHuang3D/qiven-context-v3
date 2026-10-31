// store/exchange's smoke case, on the fake, over a scratch directory of the
// case's own, the plain SQLite store over plain records: a source store
// with two lanes and a forget; verify's counts; its export whole, its
// heads, snapshot and lines; imported into an empty store: the heads, the
// counts, the size and verify equal, the -wal 0 bytes; a second import
// StoreNotEmpty; an export from a seq of one lane and of a scope set; its
// lines held with their keys imported, verify's counts the source's; each
// import's store reopened read-only and verified; an export closed by its
// own close, by its store object's move and close; each export statement's
// plan.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
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
#include "qiven/runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/exchange.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/stores/records.hpp"

namespace {

namespace fs = std::filesystem;
using Fake = qiven3::platform::fake::FakePlatform;
using Store = qiven3::store::SQLiteStore<Fake>;
using Export = qiven3::store::sqlite::SQLiteExport<Fake>;
using qiven3::store::AfterView;
using qiven3::store::Counts;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::StoreError;
using qiven3::store::Vector;
using qiven3::test::records::chain;
using qiven3::test::records::forget;
using qiven3::test::records::owned;
using qiven3::test::records::Rec;
using qiven3::test::records::views;

static_assert(qiven3::store::Export<Export>);

constexpr std::string_view held_scope = "project:qiven";
constexpr std::string_view first_lane = "implementer";
constexpr std::string_view second_lane = "reviewer";

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end; the fake
// over it, the engine heap and the caller's arena.
struct Rig {
    Rig() : root(fresh()), fake(config(root)), engine(fake), pages(std::size_t{1} << 20U), arena(pages), chars(arena) {}
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;
    ~Rig() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    static fs::path fresh() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-exchange-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    static qiven3::platform::fake::Config config(const fs::path& root) {
        qiven3::platform::fake::Config c;
        c.root = text(root);
        c.seed = 7;
        return c;
    }
    [[nodiscard]] std::string path(std::string_view name) const { return text(root / std::string(name) / "store.db"); }
    fs::path root;
    Fake fake;
    qiven3::store::Engine<Fake> engine;
    std::vector<std::byte> pages;
    qiven3::memory::Arena arena;
    qiven3::memory::Allocator<char> chars;
};

// The error a call raised, as "Kind code", or "none".
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
        } else if (!e.entry_code().empty()) {
            code = e.entry_code();
        }
        return std::string(qiven3::store::name(e.kind())) + " " + code;
    }
    return "none";
}

// A source of lines held: the heads given, then each line with its keys.
struct Held {
    std::vector<LaneHead> given;
    std::vector<Rec> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        const Rec& r = lines[at++];
        line.header.assign(r.line);
        line.has_body = r.record.has_value();
        line.body.assign(r.record.value_or(std::string()));
        return line.keys.assign(qiven3::test::records::view(r.keys));
    }
};

// An export's lines into held, and their count.
std::size_t drain(Export& e, Held* held) {
    std::vector<std::byte> pages(std::size_t{1} << 18U);
    std::size_t n = 0;
    for (;;) {
        qiven3::memory::Arena arena(pages);
        Line line(arena);
        if (!e.next(line)) {
            return n;
        }
        ++n;
        if (held != nullptr) {
            held->lines.push_back(owned(line));
        }
    }
}

std::uintmax_t wal_bytes(const std::string& path) {
    std::error_code error;
    const std::uintmax_t n = fs::file_size(fs::path(path + "-wal"), error);
    return error ? 0 : n;
}

// Two lanes, four records and a forget of B in the first, two in the
// second.
void fill(Store& store, const qiven3::memory::Allocator<char>& chars) {
    const std::vector<Rec> mine = chain(held_scope, first_lane, {"A", "B", "D", "ACC"});
    const std::vector<Rec> theirs = chain(held_scope, second_lane, {"C", "M1"});
    const std::vector<Rec> forgets = {forget("E", held_scope, first_lane, 5, "ACC", "B")};
    (void)store.append(held_scope, first_lane, std::nullopt, views(mine));
    (void)store.append(held_scope, second_lane, std::nullopt, views(theirs));
    const std::vector<qiven3::store::GuardView> guard = {qiven3::store::GuardView{held_scope, first_lane, "ACC"},
                                                         qiven3::store::GuardView{held_scope, second_lane, "M1"}};
    Vector<qiven3::store::Id> erased(chars);
    (void)store.forget(held_scope, first_lane, "ACC", views(forgets), guard, erased);
}

// Each export statement's plan, printed: no line of it a structure built
// in memory as large as its rows.
bool planned(qiven3::store::sqlite::Connection& c) {
    namespace sqlite = qiven3::store::sqlite;
    const qiven3::store::WhereView at = sqlite::site("plan", "");
    bool good = true;
    for (const std::string_view sql : {sqlite::forget_counts, sqlite::head_rows, sqlite::lane_rows}) {
        sqlite::Statement s = c.prepare(std::string("EXPLAIN QUERY PLAN ") + std::string(sql), at);
        while (s.step(at)) {
            const std::string_view line = s.text(3);
            std::cout << "store_exchange.smoke: plan: " << line << '\n';
            for (const std::string_view bad : {"TEMP B-TREE", "AUTOMATIC", "MATERIALIZE", "BLOOM FILTER"}) {
                good = good && line.find(bad) == std::string_view::npos;
            }
        }
    }
    return good;
}

bool same(const Vector<LaneHead>& a, const Vector<LaneHead>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].head.id != b[i].head.id || a[i].head.seq != b[i].head.seq ||
            a[i].scope.view() != b[i].scope.view() || a[i].lane.view() != b[i].lane.view()) {
            return false;
        }
    }
    return true;
}

}  // namespace

QIVEN_CASE(store_exchange, smoke) {
    const auto rig = std::make_unique<Rig>();
    const auto source = std::make_unique<Store>(Store::open(rig->path("from"), true, false, rig->fake));
    fill(*source, rig->chars);
    const Counts held = source->verify();
    CHECK_EQ(held.entries, 7U);
    CHECK_EQ(held.bodies, 5U);
    CHECK_EQ(held.forgets, 1U);
    CHECK_EQ(held.lanes, 2U);
    CHECK(planned(source->connection()));
    const qiven3::store::Volume from = source->size().volume;

    auto whole = std::make_unique<Export>(source->export_(std::nullopt, {}));
    Vector<LaneHead> rows(rig->chars);
    const qiven3::store::Snapshot snapshot = whole->heads(rows);
    CHECK_EQ(rows.size(), 2U);
    CHECK_EQ(snapshot.forgets, 1U);
    CHECK(snapshot.volume == from);
    const auto into = std::make_unique<Store>(Store::open(rig->path("into"), true, false, rig->fake));
    Vector<LaneHead> heads(rig->chars);
    const qiven3::store::Imported imported = into->import_(*whole, heads);
    CHECK(imported.counts == held);
    CHECK(imported.volume == from);
    CHECK(same(heads, rows));
    CHECK(into->verify() == held);
    CHECK(into->size().volume == from);
    CHECK_EQ(wal_bytes(rig->path("into")), 0U);
    whole->close();
    CHECK_EQ(raised([&] { (void)whole->heads(rows); }), "StoreError closed");

    auto again = std::make_unique<Export>(source->export_(std::nullopt, {}));
    CHECK_EQ(raised([&] { (void)into->import_(*again, heads); }), "StoreNotEmpty ");
    CHECK(into->verify() == held);

    const std::vector<AfterView> after = {AfterView{held_scope, first_lane, 2}};
    auto later = std::make_unique<Export>(source->export_(std::nullopt, after));
    CHECK_EQ(drain(*later, nullptr), 5U);
    const std::vector<std::string_view> projects = {"project:*"};
    auto all = std::make_unique<Export>(source->export_(projects, {}));
    Held copy;
    (void)all->heads(rows);
    copy.given.assign(rows.begin(), rows.end());
    CHECK_EQ(drain(*all, &copy), 7U);
    const std::vector<std::string_view> others = {"project:other"};
    auto none = std::make_unique<Export>(source->export_(others, {}));
    CHECK_EQ(none->heads(rows).forgets, 0U);
    CHECK_EQ(rows.size(), 0U);
    CHECK_EQ(drain(*none, nullptr), 0U);

    // The lines held with their keys imported; each import's store reopened.
    const auto copied = std::make_unique<Store>(Store::open(rig->path("copied"), true, false, rig->fake));
    CHECK(copied->import_(copy, heads).counts == held);
    CHECK_EQ(heads.size(), copy.given.size());
    CHECK(copied->verify() == held);
    CHECK(copied->close().done && into->close().done);
    for (const std::string_view name : {"into", "copied"}) {
        const auto reader = std::make_unique<Store>(Store::open(rig->path(name), false, true, rig->fake));
        CHECK(reader->verify() == held);
    }

    // A move closes the moved-from object's exports, a close its own.
    auto open = std::make_unique<Export>(source->export_(std::nullopt, {}));
    const auto moved = std::make_unique<Store>(std::move(*source));
    CHECK_EQ(raised([&] { (void)drain(*open, nullptr); }), "StoreError closed");
    auto last = std::make_unique<Export>(moved->export_(std::nullopt, {}));
    CHECK_EQ(drain(*last, nullptr), 7U);
    CHECK(moved->close().done);
    CHECK_EQ(raised([&] { (void)last->heads(rows); }), "StoreError closed");
}
