// store/exchange's states, on the fake, over a scratch directory of the case's own, the plain SQLite store over
// plain records. maximum: a record of a 64 KiB body exported, imported and verified, each counted peak within
// 1 MiB. large: a lane of 400 entries
// exported straight into an import, each peak within 1 MiB and no greater than a lane of 100's (seqs of 3 digits).
// busy: an import while another object holds the write lock, StoreBusy after the busy wait, nothing written;
// then imported.
// threads: an export's heads and next, and an import from it, from another thread, StoreError thread; the
// export then read whole on its own. closing: exports closed by their own close in any order, by
// their store object's close, move, move-assignment and end; an export moved, the moved-from closed, the
// moved-to read whole and then closed with its store object. rollback: a source that throws after its third
// line, and a refusal after 60 records of 64 KiB bodies spilled pages into the -wal, by the hooks once
// written and by the admit hook at the heads: each rolled back, the -wal 0 bytes after it, the heads cleared,
// the store empty; the same source then imported. not_empty: an import into a store holding an entry,
// StoreNotEmpty, the store unchanged. Each store written verified, then reopened read-only and verified.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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

using namespace qiven3::test::records;  // NOLINT(google-build-using-namespace)
namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using qiven3::memory::Vector;
using qiven3::store::Counts;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::StoreError;
using Fake = qiven3::platform::fake::FakePlatform;
using Arena = qiven3::memory::Arena;
using Chars = qiven3::memory::Allocator<char>;
using Store = qiven3::store::SQLiteStore<Fake>;
using Export = sqlite::SQLiteExport<Fake>;
using Engine = qiven3::store::Engine<Fake>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::string_view reviewer_lane = "reviewer";
constexpr std::size_t own_bytes = qiven3::store::operation_base;
constexpr std::size_t max_body = std::size_t{64} << 10U;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::uintmax_t wal_bytes(const std::string& path) {
    std::error_code error;
    const std::uintmax_t n = fs::file_size(fs::path(path + "-wal"), error);
    return error ? 0 : n;
}

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

// A source of lines held: the heads given, then each line; past fail_at
// lines, its next throws.
struct Held {
    std::vector<LaneHead> given;
    std::vector<Rec> lines;
    std::size_t at = 0;
    std::size_t fail_at = 0;  // 0: never
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (fail_at != 0 && at == fail_at) {
            throw std::runtime_error("the source failed");
        }
        if (at == lines.size()) {
            return false;
        }
        const Rec& r = lines[at++];
        line.header.assign(r.line);
        line.has_body = r.record.has_value();
        line.body.assign(r.record.value_or(std::string()));
        return line.keys.assign(view(r.keys));
    }
    // A lane of records, its head the last.
    void add(const std::vector<Rec>& lane, std::string_view scope, std::string_view name) {
        lines.insert(lines.end(), lane.begin(), lane.end());
        given.push_back(lane_head(scope, name, id_of(lane.back()), static_cast<std::int64_t>(lane.size())));
    }
};

// The import's hooks: the -wal's bytes once every line is in, and a
// refusal there where asked.
struct Probe {
    const std::string* path = nullptr;
    std::uintmax_t* seen = nullptr;
    bool refuse = false;
    void begun() const noexcept {}
    void written() const {
        *seen = wal_bytes(*path);
        if (refuse) {
            throw StoreError(qiven3::store::Code::internal, "import", *path, "refused once written");
        }
    }
    void committed() const noexcept {}
    void rebuilt() const noexcept {}
};
using Probed = qiven3::store::SQLiteStore<Fake, Probe>;

// An admit hook refusing the heads at an import's end, any held.
struct HeadsRefused : qiven3::store::NoAdmit {
    static void heads(std::span<const LaneHead> source, std::span<const LaneHead> /*given*/,
                      const qiven3::store::AtView& /*at*/) {
        if (!source.empty()) {
            throw StoreError(qiven3::store::Code::internal, "import", "", "heads refused");
        }
    }
};

// A scratch directory of the case's own, removed at its end; the fake over
// it, the engine heap and the caller's arena.
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
        const std::string name = "qiven3-exchange-s-" + std::to_string(draw()) + std::to_string(draw());
        fs::path out = fs::temp_directory_path() / name;
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
    [[nodiscard]] std::unique_ptr<Store> made(std::string_view name) {
        return std::make_unique<Store>(Store::open(path(name), true, false, fake));
    }
    fs::path root;
    Fake fake;
    Engine engine;
    std::vector<std::byte> pages;
    Arena arena;
    Chars chars;
};

// A chain of n records on implementer's lane from seq 1, each with a body;
// ids of one width: records of one size.
std::vector<Rec> claims(std::size_t n) {
    std::vector<Rec> out;
    for (std::size_t i = 0; i < n; ++i) {
        const std::optional<std::string_view> parent =
            i == 0 ? std::nullopt : std::optional<std::string_view>(id_of(out.back()));
        out.push_back(rec("c" + std::to_string(1000000 + i), qiven_scope, implementer_lane,
                          static_cast<std::int64_t>(i) + 1, parent));
    }
    return out;
}

// A chain of n records on reviewer's lane from seq 1, each of a 64 KiB body.
std::vector<Rec> modules(std::size_t n) {
    std::vector<Rec> out;
    for (std::size_t i = 0; i < n; ++i) {
        const std::optional<std::string_view> parent =
            i == 0 ? std::nullopt : std::optional<std::string_view>(id_of(out.back()));
        out.push_back(big("m" + std::to_string(100 + i), qiven_scope, reviewer_lane, static_cast<std::int64_t>(i) + 1,
                          parent, max_body));
    }
    return out;
}

// The store at path reopened read-only, its verify's counts.
Counts reread(Rig& rig, const std::string& path) {
    const auto reader = std::make_unique<Store>(Store::open(path, false, true, rig.fake));
    return reader->verify();
}

// An export's lines read, each on a fresh arena; their count and the
// greatest of their arenas' peaks.
std::pair<std::size_t, std::size_t> drain(Export& e) {
    std::vector<std::byte> pages(std::size_t{1} << 18U);
    std::size_t n = 0;
    std::size_t most = 0;
    for (;;) {
        Arena arena(pages);
        Line line(arena);
        if (!e.next(line)) {
            return {n, most};
        }
        ++n;
        most = std::max(most, arena.peak());
    }
}

// A lane of claims appended in batches of 50, its export imported
// straight into an empty store; the export's, import's and verify's
// peaks.
struct Peaks {
    std::size_t exported = 0;
    std::size_t imported = 0;
    std::size_t verified = 0;
    std::uint64_t entries = 0;
    bool same = false;  // verify's counts equal
};
Peaks round_trip(Rig& rig, std::size_t n, std::string_view name) {
    const std::vector<Rec> lane = claims(n);
    auto from = rig.made(std::string(name) + "-from");
    for (std::size_t i = 0; i < n; i += 50) {
        const std::vector<Rec> batch(lane.begin() + static_cast<std::ptrdiff_t>(i),
                                        lane.begin() + static_cast<std::ptrdiff_t>(std::min(n, i + 50)));
        const std::optional<std::string> parent = i == 0 ? std::nullopt : std::optional(id_of(lane[i - 1]));
        (void)from->append(qiven_scope, implementer_lane, parent, views(batch));
    }
    auto into = rig.made(std::string(name) + "-into");
    Peaks p;
    {
        auto x = std::make_unique<Export>(from->export_(std::nullopt, {}));
        Vector<LaneHead> heads(rig.chars);
        const qiven3::store::Imported i = into->import_(*x, heads);
        p.exported = x->peak();
        p.imported = into->peak();
        p.entries = i.counts.entries;
    }
    const Counts held = from->verify();
    p.same = into->verify() == held;
    p.verified = into->peak();
    p.same = p.same && reread(rig, rig.path(std::string(name) + "-into")) == held;
    return p;
}

}  // namespace

QIVEN_CASE(store_exchange, maximum) {
    const auto rig = std::make_unique<Rig>();
    const std::vector<Rec> mx = modules(1);
    REQUIRE(mx[0].record->size() == max_body);
    const auto from = rig->made("from");
    (void)from->append(qiven_scope, reviewer_lane, std::nullopt, views(mx));
    auto x = std::make_unique<Export>(from->export_(std::nullopt, {}));
    const auto [lines, caller] = drain(*x);
    CHECK_EQ(lines, 1U);
    const std::size_t exported = x->peak();
    x = std::make_unique<Export>(from->export_(std::nullopt, {}));
    const auto into = rig->made("into");
    Vector<LaneHead> heads(rig->chars);
    (void)into->import_(*x, heads);
    const std::size_t imported = into->peak();
    const Counts counts = into->verify();
    const std::size_t verified = into->peak();
    CHECK(counts == from->verify());
    CHECK_EQ(counts.bodies, 1U);
    CHECK(reread(*rig, rig->path("into")) == counts);
    CHECK(exported <= own_bytes);
    CHECK(imported <= own_bytes);
    CHECK(verified <= own_bytes);
    std::cout << "store_exchange.maximum: record " << mx[0].record->size() << " bytes; next " << exported
              << ", import " << imported << ", verify " << verified << " of " << own_bytes << "; the caller's line "
              << caller << "\n";
}

QIVEN_CASE(store_exchange, large) {
    const auto rig = std::make_unique<Rig>();
    const Peaks small = round_trip(*rig, 100, "small");
    const Peaks large = round_trip(*rig, 400, "large");
    CHECK(small.same && large.same);
    CHECK_EQ(small.entries, 100U);
    CHECK_EQ(large.entries, 400U);
    CHECK(large.exported <= own_bytes);
    CHECK(large.imported <= own_bytes);
    CHECK(large.verified <= own_bytes);
    CHECK(large.exported <= small.exported);
    CHECK(large.imported <= small.imported);
    CHECK(large.verified <= small.verified);
    std::cout << "store_exchange.large: 100 and 400 entries: export " << small.exported << " and " << large.exported
              << ", import " << small.imported << " and " << large.imported << ", verify " << small.verified
              << " and " << large.verified << "\n";
}

QIVEN_CASE(store_exchange, busy) {
    const auto rig = std::make_unique<Rig>();
    Held held;
    held.add(claims(3), qiven_scope, implementer_lane);
    const auto into = rig->made("into");
    const auto other = std::make_unique<Store>(Store::open(rig->path("into"), false, false, rig->fake));
    other->connection().exec("BEGIN IMMEDIATE", sqlite::site("hold", ""));
    Vector<LaneHead> heads(rig->chars);
    const auto start = std::chrono::steady_clock::now();
    CHECK_EQ(raised([&] { (void)into->import_(held, heads); }), "StoreBusy ");
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(waited >= 4.5);
    CHECK(!into->connection().in_transaction());
    other->connection().exec("ROLLBACK", sqlite::site("hold", ""));
    CHECK(into->verify() == Counts{});
    held.at = 0;
    CHECK_EQ(into->import_(held, heads).counts.entries, 3U);
    CHECK(reread(*rig, rig->path("into")) == into->verify());
    std::cout << "store_exchange.busy: StoreBusy after " << waited << " s\n";
}

QIVEN_CASE(store_exchange, threads) {
    const auto rig = std::make_unique<Rig>();
    Held held;
    held.add(claims(3), qiven_scope, implementer_lane);
    const auto from = rig->made("from");
    Vector<LaneHead> written(rig->chars);
    (void)from->import_(held, written);
    CHECK(reread(*rig, rig->path("from")) == from->verify());
    auto x = std::make_unique<Export>(from->export_(std::nullopt, {}));
    const auto into = rig->made("into");
    std::vector<std::string> got;
    std::thread other([&] {
        std::vector<std::byte> pages(std::size_t{1} << 16U);
        Arena arena(pages);
        Vector<LaneHead> rows{Chars(arena)};
        got.push_back(raised([&] { (void)x->heads(rows); }));
        Line line(arena);
        got.push_back(raised([&] { (void)x->next(line); }));
        Vector<LaneHead> heads{Chars(arena)};
        got.push_back(raised([&] { (void)into->import_(*x, heads); }));
    });
    other.join();
    REQUIRE(got.size() == 3U);
    CHECK_EQ(got[0], "StoreError thread");
    CHECK_EQ(got[1], "StoreError thread");
    CHECK_EQ(got[2], "StoreError thread");
    Vector<LaneHead> rows(rig->chars);
    CHECK_EQ(x->heads(rows).volume.count, 3U);
    CHECK_EQ(drain(*x).first, 3U);
    std::cout << "store_exchange.threads: from another thread: " << got[0] << ", " << got[1] << ", " << got[2]
              << "\n";
}

QIVEN_CASE(store_exchange, closing) {
    const auto rig = std::make_unique<Rig>();
    Held held;
    held.add(claims(4), qiven_scope, implementer_lane);
    auto from = rig->made("from");
    Vector<LaneHead> rows(rig->chars);
    (void)from->import_(held, rows);
    CHECK(reread(*rig, rig->path("from")) == from->verify());
    const auto closed = [&](Export& e) { return raised([&] { (void)e.heads(rows); }) == "StoreError closed"; };
    // Its own close, in any order: the middle of the list, its first, its last.
    auto e1 = std::make_unique<Export>(from->export_(std::nullopt, {}));
    auto e2 = std::make_unique<Export>(from->export_(std::nullopt, {}));
    auto e3 = std::make_unique<Export>(from->export_(std::nullopt, {}));
    auto e4 = std::make_unique<Export>(from->export_(std::nullopt, {}));
    e2->close();
    e4->close();
    e1->close();
    CHECK(closed(*e1) && closed(*e2) && closed(*e4));
    CHECK_EQ(drain(*e3).first, 4U);
    e2.reset();
    // A moved export: the moved-from closed, the moved-to reads on and is
    // closed with its store object; a move-assignment closes the one it
    // replaces.
    auto e5 = std::make_unique<Export>(from->export_(std::nullopt, {}));
    CHECK_EQ(drain(*e5).first, 4U);
    auto e6 = std::make_unique<Export>(from->export_(std::nullopt, {}));
    auto moved = std::make_unique<Export>(std::move(*e6));
    CHECK(closed(*e6));
    CHECK_EQ(drain(*moved).first, 4U);
    auto e7 = std::make_unique<Export>(from->export_(std::nullopt, {}));
    *e5 = std::move(*e7);
    CHECK(closed(*e7));
    CHECK_EQ(e5->heads(rows).volume.count, 4U);
    e6.reset();
    // Its store object's close closes every one open.
    CHECK(from->close().done);
    CHECK(closed(*e3) && closed(*e5) && closed(*moved));
    // Its store object's move-assignment closes both objects' exports.
    from = std::make_unique<Store>(Store::open(rig->path("from"), false, false, rig->fake));
    auto other = std::make_unique<Store>(Store::open(rig->path("from"), false, true, rig->fake));
    auto mine = std::make_unique<Export>(from->export_(std::nullopt, {}));
    auto theirs = std::make_unique<Export>(other->export_(std::nullopt, {}));
    *from = std::move(*other);
    CHECK(closed(*mine) && closed(*theirs));
    // Its store object's end: the export closed, and ended after it.
    auto last = std::make_unique<Export>(from->export_(std::nullopt, {}));
    CHECK_EQ(drain(*last).first, 4U);
    auto ending = std::make_unique<Export>(from->export_(std::nullopt, {}));
    from.reset();
    CHECK(closed(*last) && closed(*ending));
    ending.reset();
    last.reset();
}

QIVEN_CASE(store_exchange, rollback) {
    const auto rig = std::make_unique<Rig>();
    Held held;
    held.add(claims(5), qiven_scope, implementer_lane);
    held.fail_at = 3;
    const auto into = rig->made("into");
    Vector<LaneHead> heads(rig->chars);
    heads.push_back(lane_head(qiven_scope, implementer_lane, id_of(claims(1)[0]), 1));
    bool threw = false;
    try {
        (void)into->import_(held, heads);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(heads.empty());
    CHECK(!into->connection().in_transaction());
    CHECK_EQ(wal_bytes(rig->path("into")), 0U);
    CHECK(into->verify() == Counts{});
    held.fail_at = 0;
    held.at = 0;
    CHECK_EQ(into->import_(held, heads).counts.entries, 5U);

    // 60 entries of 64 KiB: their pages spill into the -wal before COMMIT.
    Held big;
    big.add(modules(60), qiven_scope, reviewer_lane);
    std::uintmax_t seen = 0;
    const std::string path = rig->path("big");
    auto probed = std::make_unique<Probed>(Probed::open(path, true, false, rig->fake, {}, Probe{&path, &seen, true}));
    CHECK_EQ(raised([&] { (void)probed->import_(big, heads); }), "StoreError internal");
    const std::uintmax_t spilled = seen;
    CHECK(spilled > 0U);
    CHECK_EQ(wal_bytes(path), 0U);
    CHECK(heads.empty());
    CHECK(probed->verify() == Counts{});
    probed.reset();
    // The heads refused by the admit hook at the end, after the spill.
    big.at = 0;
    probed = std::make_unique<Probed>(Probed::open(path, false, false, rig->fake, {}, Probe{&path, &seen, false}));
    CHECK_EQ(raised([&] { (void)probed->import_(big, heads, HeadsRefused{}); }), "StoreError internal");
    CHECK_EQ(wal_bytes(path), 0U);
    CHECK(heads.empty());
    CHECK(probed->verify() == Counts{});
    big.at = 0;
    CHECK_EQ(probed->import_(big, heads).counts.entries, 60U);
    CHECK_EQ(heads.size(), 1U);
    CHECK_EQ(wal_bytes(path), 0U);
    const Counts counts = probed->verify();
    CHECK_EQ(counts.entries, 60U);
    CHECK(reread(*rig, path) == counts);
    CHECK(reread(*rig, rig->path("into")) == into->verify());
    std::cout << "store_exchange.rollback: the -wal held " << spilled << " bytes once written, 0 after the refusal; "
              << seen << " before the import's COMMIT\n";
}

QIVEN_CASE(store_exchange, not_empty) {
    const auto rig = std::make_unique<Rig>();
    Held held;
    held.add(claims(2), qiven_scope, implementer_lane);
    const auto into = rig->made("into");
    const std::vector<Rec> one = claims(1);
    (void)into->append(qiven_scope, implementer_lane, std::nullopt, views(one));
    const Counts before = into->verify();
    Vector<LaneHead> heads(rig->chars);
    CHECK_EQ(raised([&] { (void)into->import_(held, heads); }), "StoreNotEmpty ");
    CHECK(heads.empty());
    CHECK(into->verify() == before);
    CHECK_EQ(before.entries, 1U);
    CHECK_EQ(held.at, 0U);  // refused before its first line
    CHECK(reread(*rig, rig->path("into")) == before);
}
