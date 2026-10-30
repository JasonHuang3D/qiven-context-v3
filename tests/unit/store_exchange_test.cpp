// store/exchange's smoke case, on the fake, over a scratch directory of the
// case's own: a source store with two lanes and a forget; verify's counts;
// its export whole, its heads, snapshot and lines; imported into an empty
// store: the heads, the counts, the size and verify equal, the -wal 0
// bytes; a second import StoreNotEmpty; an export from a seq of one lane
// and of a scope set; a source whose heads differ refused, nothing left;
// an export closed by its own close, by its store object's move and close;
// each export statement's plan.

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

#include "checked/stores.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/exchange.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

namespace fs = std::filesystem;
using Fake = qiven3::platform::fake::FakePlatform;
using Store = qiven3::checked::SQLiteStore<Fake>;
using Export = qiven3::store::sqlite::SQLiteExport<Fake>;
using qiven3::store::AfterView;
using qiven3::store::Counts;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::StoreError;
using qiven3::store::Vector;
using qiven3::test::stage_book::Stored;
using qiven3::test::stage_book::views;

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
// over it, the engine heap, the fixtures and the caller's arena.
struct Rig {
    Rig()
        : root(fresh()), fake(config(root)), engine(fake), book(fake), pages(std::size_t{1} << 20U), arena(pages),
          chars(arena) {}
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
    qiven3::test::stage_book::Book book;
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

// A source of lines held: the heads given, then each line.
struct Held {
    std::vector<LaneHead> given;
    std::vector<std::pair<std::string, std::optional<std::string>>> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        line.header.assign(lines[at].first);
        line.has_body = lines[at].second.has_value();
        line.body.assign(lines[at].second.value_or(std::string()));
        const std::optional<std::string_view> record =
            lines[at].second ? std::optional<std::string_view>(*lines[at].second) : std::nullopt;
        ++at;
        return line.keys.assign(qiven3::test::stage_book::keyed(lines[at - 1].first, record).keys);
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
            held->lines.emplace_back(std::string(line.header), line.has_body ? std::optional<std::string>(
                                                                                   std::string(line.body))
                                                                             : std::nullopt);
        }
    }
}

std::uintmax_t wal_bytes(const std::string& path) {
    std::error_code error;
    const std::uintmax_t n = fs::file_size(fs::path(path + "-wal"), error);
    return error ? 0 : n;
}

// Two lanes, four entries and a forget in the first, two in the second.
void fill(Store& store, const qiven3::test::stage_book::Book& book, const qiven3::memory::Allocator<char>& chars) {
    const std::vector<Stored> mine = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
    const std::vector<Stored> theirs = {book.stored("C"), book.stored("M1")};
    const std::vector<Stored> forgets = {book.stored("E")};
    (void)store.append(held_scope, first_lane, std::nullopt, views(mine));
    (void)store.append(held_scope, second_lane, std::nullopt, views(theirs));
    const std::string acc = qiven3::test::stage_book::id_of(mine[3]);
    const std::string m1 = qiven3::test::stage_book::id_of(theirs[1]);
    const std::vector<qiven3::store::GuardView> guard = {qiven3::store::GuardView{held_scope, first_lane, acc},
                                                         qiven3::store::GuardView{held_scope, second_lane, m1}};
    Vector<qiven3::store::Id> erased(chars);
    (void)store.forget(held_scope, first_lane, acc, views(forgets), guard, erased);
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

QIVEN3_CASE(store_exchange, smoke) {
    const auto rig = std::make_unique<Rig>();
    const auto source = std::make_unique<Store>(Store::open(rig->path("from"), true, false, rig->fake));
    fill(*source, rig->book, rig->chars);
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

    // The heads given short of the lines': head_mismatch, nothing left.
    copy.given.pop_back();
    const auto refused = std::make_unique<Store>(Store::open(rig->path("refused"), true, false, rig->fake));
    CHECK_EQ(raised([&] { (void)refused->import_(copy, heads); }), "EntryInvalid head_mismatch");
    CHECK(heads.empty());
    CHECK(refused->verify() == Counts{});
    CHECK_EQ(wal_bytes(rig->path("refused")), 0U);

    // A move closes the moved-from object's exports, a close its own.
    auto open = std::make_unique<Export>(source->export_(std::nullopt, {}));
    const auto moved = std::make_unique<Store>(std::move(*source));
    CHECK_EQ(raised([&] { (void)drain(*open, nullptr); }), "StoreError closed");
    auto last = std::make_unique<Export>(moved->export_(std::nullopt, {}));
    CHECK_EQ(drain(*last, nullptr), 7U);
    CHECK(moved->close().done);
    CHECK_EQ(raised([&] { (void)last->heads(rows); }), "StoreError closed");
}
