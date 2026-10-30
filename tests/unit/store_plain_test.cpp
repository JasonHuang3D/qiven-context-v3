// store's plain path, in both stores with no admit hook (NoAdmit), over plain
// keys of lines no parser takes, each write followed by verify and, on
// SQLite, a reopen and verify again, and what each store then gives compared.
// no_body_append, no_body_import: a record with no body commits and opens.
// forget_no_key_import, forget_rule_import: a forget whose target has no body
// key, is another scope's, is a forget or has an earlier forget, refused.
// id_held: an id held or repeated in a call, by append, forget and import,
// refused, the order's refusal first. keys_alike_import, keys_alike_append: a
// body, none, a body key forgotten, the same by a scan, an export and by id.
// many_keys_import: body keys with no record past any list of them the
// import's pages could keep. lost_body: a body row lost or stray, the count
// equal or not, refused by a scan, an export and verify on the open store,
// then by open.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>  // pair
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"
#include "store/engine/sqlite.hpp"
#include "store/memory.hpp"
#include "store/memory_state.hpp"
#include "store/sqlite_store.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

namespace fs = std::filesystem;
namespace sqlite = qiven3::store::sqlite;
using qiven3::store::Counts;
using qiven3::store::EntryView;
using qiven3::store::KeysView;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::NoAdmit;
using qiven3::store::Vector;
using qiven3::test::stage_book::Fake;
using qiven3::test::stage_book::lane_head;
using Arena = qiven3::memory::Arena;
using Chars = qiven3::memory::Allocator<char>;
using String = qiven3::memory::String;
using Store = qiven3::store::SQLiteStore<Fake>;
using Parent = std::optional<std::string_view>;

constexpr std::size_t state_bytes = std::size_t{8} << 20U;
constexpr std::size_t page_bytes = std::size_t{1} << 20U;
constexpr qiven3::store::WhereView test_at = sqlite::site("test", "scratch");

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A scratch directory of the case's own, removed at its end, a fake on it.
struct Site {
    static qiven3::platform::fake::Config config(const fs::path& root) {
        qiven3::platform::fake::Config out;
        out.root = text(root);
        out.seed = 7;
        return out;
    }
    static fs::path fresh() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        fs::path out = fs::temp_directory_path() / ("qiven3-plain-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }
    Site() : root(fresh()), fake(config(root)), engine(fake) {}
    Site(const Site&) = delete;
    Site& operator=(const Site&) = delete;
    Site(Site&&) = delete;
    Site& operator=(Site&&) = delete;
    ~Site() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    fs::path root;
    Fake fake;
    qiven3::store::Engine<Fake> engine;
};

struct Given {
    std::string line;
    std::optional<std::string> record;
    KeysView keys;
};

// A plain record on the scope "plain", its body key and its record as given.
Given note(std::string_view id, std::string_view lane, std::int64_t seq, Parent parent, bool key, bool record) {
    return Given{std::string("line ").append(id), record ? std::optional<std::string>("record") : std::nullopt,
                 KeysView{id, "plain", lane, "note", seq, parent, {}, key}};
}
Given gone(std::string_view id, std::string_view lane, std::int64_t seq, Parent parent, std::string_view target) {
    return Given{std::string("line ").append(id), std::nullopt,
                 KeysView{id, "plain", lane, "forget", seq, parent, target, false}};
}

// An import's source of lines with their keys as given.
struct PlainSource {
    std::vector<LaneHead> given;
    const std::vector<Given>* lines = nullptr;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines->size()) {
            return false;
        }
        const Given& g = (*lines)[at++];
        line.header.assign(g.line);
        line.has_body = g.record.has_value();
        line.body.assign(g.record.value_or(std::string()));
        line.forgotten = false;
        return line.keys.assign(g.keys);
    }
};

// The writes of a case: lines imported or appended each alone, then a first
// call of lines together, then a forget given; the last refused "code index id".
struct Plan {
    bool import = false;
    std::vector<Given> lines;
    std::vector<LaneHead> heads;
    std::optional<Given> forget;
    std::vector<Given> together;
    std::string_view refused;
};

std::array<EntryView, 1> one(const Given& g) {
    return {EntryView{g.line, g.record ? std::optional<std::string_view>(*g.record) : std::nullopt, g.keys}};
}

template <class S>
void write(S& s, const Plan& p, Arena& arena) {
    if (p.import) {
        PlainSource source{p.heads, &p.lines};
        Vector<LaneHead> rows{Chars(arena)};
        (void)s.import_(source, rows, NoAdmit{});
    } else {
        for (const Given& g : p.lines) {
            (void)s.append(g.keys.scope, g.keys.lane, g.keys.parent, one(g), NoAdmit{});
        }
    }
    if (!p.together.empty()) {
        std::vector<EntryView> call;
        for (const Given& g : p.together) {
            call.push_back(one(g)[0]);
        }
        (void)s.append("plain", "one", {}, std::span<const EntryView>(call), NoAdmit{});
    }
    if (p.forget) {
        const KeysView& k = p.forget->keys;
        const KeysView& last = p.lines.back().keys;  // the one lane held
        const std::array<qiven3::store::GuardView, 1> guard = {{{last.scope, last.lane, last.id}}};
        Vector<qiven3::store::Id> erased{Chars(arena)};
        (void)s.forget(k.scope, k.lane, k.parent, one(*p.forget), guard, erased, NoAdmit{});
    }
}

std::string keys_text(const KeysView& k) {
    std::string out(k.id);
    out.append(" ").append(k.scope).append(" ").append(k.lane).append(" ").append(k.kind);
    out.append(" ").append(std::to_string(k.seq)).append(" ").append(k.parent.value_or("-"));
    out.append(" ").append(k.target.value_or("-")).append(k.body ? " body\n" : " none\n");
    return out;
}

// Each entry's keys by a scan and by an export.
template <class S>
std::string scanned(S& s, Arena& arena) {
    std::string out;
    qiven3::store::Record r{Chars(arena)};
    for (auto scan = s.scan({}); scan.next(r);) {
        out += keys_text(r.keys());
    }
    return out;
}
template <class S>
std::string exported(S& s, Arena& arena) {
    std::string out;
    auto source = s.export_(std::nullopt, {});
    Vector<LaneHead> rows{Chars(arena)};
    (void)source.heads(rows);
    Line line(arena);
    while (source.next(line)) {
        out += keys_text(line.keys.view());
    }
    source.close();
    return out;
}

// What s gives: its keys, by a scan and an export alike, then by id, through
// the plan's lines where all were written, whether it holds a record.
template <class S>
std::string given(S& s, const Plan& p, Arena& arena) {
    const std::string keys = scanned(s, arena);
    std::string out = keys == exported(s, arena) ? keys : std::string("a scan and an export differ\n");
    String record{Chars(arena)};
    for (const Given& g : p.refused.empty() ? p.lines : std::vector<Given>{}) {
        out.append(g.keys.id).append(s.body(g.keys.id, record) ? " record\n" : " no record\n");
    }
    return out;
}

// What a StoreError f raised says, its kind, its corrupt code and its detail.
template <class F>
std::string said(const F& f) {
    try {
        f();
    } catch (const qiven3::store::StoreError& e) {
        if (e.kind() == qiven3::store::Kind::entry_invalid) {
            return std::string(e.entry_code()) + " " + std::to_string(e.index()) + " " + std::string(e.id());
        }
        const std::optional<qiven3::store::Corrupt> c = e.corrupt();
        return std::string(qiven3::store::name(e.kind())) + " " +
               std::string(c ? qiven3::store::name(*c) : std::string_view()) + " [" + std::string(e.detail()) + "]";
    }
    return {};
}

// p's writes on s, refused as it names, if it names a refusal.
template <class S>
void attempt(qiven3::test::Context& qiven3_ctx, S& s, const Plan& p, Arena& arena) {
    if (p.refused.empty()) {
        write(s, p, arena);
    } else {
        CHECK_EQ(said([&] { write(s, p, arena); }), std::string(p.refused));
    }
}

// p on SQLite, verified to counts, reopened and verified to them again; what
// it gave, the same after the reopen.
std::string on_sqlite(qiven3::test::Context& qiven3_ctx, const Plan& p, const Counts& counts) {
    std::vector<std::byte> pages(page_bytes);
    const auto site = std::make_unique<Site>();
    const std::string path = text(site->root / "p" / "store.db");
    std::string gave;
    {
        const auto s = std::make_unique<Store>(Store::open(path, true, false, site->fake));
        Arena arena(pages);
        attempt(qiven3_ctx, *s, p, arena);
        CHECK(s->verify() == counts);
        gave = given(*s, p, arena);
        (void)s->close();
    }
    const auto again = std::make_unique<Store>(Store::open(path, false, false, site->fake));
    Arena arena(pages);
    CHECK(again->verify() == counts);
    CHECK(given(*again, p, arena) == gave);
    (void)again->close();
    return gave;
}

// p on both stores, each store giving the same.
void on_both(qiven3::test::Context& qiven3_ctx, const Plan& p, const Counts& counts) {
    const auto fake = std::make_unique<Fake>(qiven3::platform::fake::Config{});
    auto heap = fake->pages(state_bytes);
    const auto st = std::make_unique<qiven3::store::state::State>(heap.bytes(), qiven3::store::Limits{});
    qiven3::store::MemoryStore<Fake> s(*st, *fake);
    std::vector<std::byte> pages(page_bytes);
    Arena arena(pages);
    attempt(qiven3_ctx, s, p, arena);
    CHECK(s.verify() == counts);
    CHECK_EQ(given(s, p, arena), on_sqlite(qiven3_ctx, p, counts));
}

}  // namespace

QIVEN3_CASE(store_plain, no_body_append) {
    Plan p;
    p.lines = {note("n1", "one", 1, {}, false, false)};
    on_both(qiven3_ctx, p, Counts{1, 0, 0, 1});
}

QIVEN3_CASE(store_plain, no_body_import) {
    Plan p;
    p.import = true;
    p.lines = {note("n1", "one", 1, {}, false, false)};
    p.heads = {lane_head("plain", "one", "n1", 1)};
    on_both(qiven3_ctx, p, Counts{1, 0, 0, 1});
}

QIVEN3_CASE(store_plain, forget_no_key_import) {
    Plan p;
    p.import = true;
    p.lines = {note("t1", "one", 1, {}, false, false), gone("f1", "two", 1, {}, "t1")};
    p.heads = {lane_head("plain", "one", "t1", 1), lane_head("plain", "two", "f1", 1)};
    p.refused = "target 1 f1";
    on_both(qiven3_ctx, p, Counts{});
}

QIVEN3_CASE(store_plain, forget_rule_import) {
    const Given t1 = note("t1", "one", 1, {}, true, false);
    const Given f1 = gone("f1", "two", 1, {}, "t1");
    Given elsewhere = f1;
    elsewhere.keys.scope = "other";
    const std::array<std::pair<std::vector<Given>, std::string_view>, 4> rows = {{
        {{t1, f1, gone("f2", "two", 2, "f1", "t1")}, "target 2 f2"},  // a second forget
        {{t1, f1, gone("f2", "two", 2, "f1", "f1")}, "target 2 f2"},  // a forget of a forget
        {{t1, elsewhere}, "target 1 f1"},                             // another scope's
        {{gone("f1", "two", 1, {}, "f1")}, "target 0 f1"},            // its own target
    }};
    for (const auto& [lines, refused] : rows) {
        on_both(qiven3_ctx, Plan{true, lines, {}, {}, {}, refused}, Counts{});
    }
}

QIVEN3_CASE(store_plain, id_held) {
    const Given a1 = note("a1", "one", 1, {}, true, true);
    const Given a1_two = note("a1", "two", 1, {}, true, true);
    const Given a1_three = note("a1", "three", 1, {}, true, true);
    const Counts held{1, 1, 0, 1};
    on_both(qiven3_ctx, Plan{false, {a1, a1_two}, {}, {}, {}, "id_held 0 a1"}, held);
    on_both(qiven3_ctx, Plan{false, {a1, note("a1", "one", 1, "a1", true, true)}, {}, {}, {}, "seq 0 a1"}, held);
    on_both(qiven3_ctx, Plan{false, {}, {}, {}, {a1, note("a1", "one", 2, "a1", true, true)}, "id_held 1 a1"}, {});
    on_both(qiven3_ctx, Plan{false, {a1}, {}, gone("a1", "two", 1, {}, "a1"), {}, "id_held 0 a1"}, held);
    on_both(qiven3_ctx, Plan{true, {a1, a1_two, a1_three}, {}, {}, {}, "id_held 1 a1"}, Counts{});
    on_both(qiven3_ctx, Plan{true, {a1, a1}, {}, {}, {}, "parent 1 a1"}, Counts{});
}

QIVEN3_CASE(store_plain, keys_alike_import) {
    Plan p;
    p.import = true;
    p.lines = {note("a1", "one", 1, {}, true, true), note("a2", "one", 2, "a1", false, false),
               note("a3", "one", 3, "a2", true, false), gone("f1", "two", 1, {}, "a3")};
    p.heads = {lane_head("plain", "one", "a3", 3), lane_head("plain", "two", "f1", 1)};
    on_both(qiven3_ctx, p, Counts{4, 1, 1, 2});
}

QIVEN3_CASE(store_plain, keys_alike_append) {
    Plan p;
    p.lines = {note("a1", "one", 1, {}, true, true), note("a2", "one", 2, "a1", false, false),
               note("a3", "one", 3, "a2", true, true)};
    p.forget = gone("f1", "two", 1, {}, "a3");
    on_both(qiven3_ctx, p, Counts{4, 1, 1, 2});
}

// 10,000 entries with a body key, each with a forget of its own.
QIVEN3_CASE(store_plain, many_keys_import) {
    constexpr std::size_t count = 10000;
    std::vector<std::string> ids;  // the keys' views: an even index a target, the next its forget
    for (std::size_t i = 0; i < 2 * count; ++i) {
        ids.push_back("i" + std::to_string(i));
    }
    Plan p;
    p.import = true;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t t = 2 * i;
        const auto seq = static_cast<std::int64_t>(i) + 1;
        p.lines.push_back(note(ids[t], "one", seq, i > 0 ? Parent(ids[t - 2]) : std::nullopt, true, false));
        p.lines.push_back(gone(ids[t + 1], "two", seq, i > 0 ? Parent(ids[t - 1]) : std::nullopt, ids[t]));
    }
    p.heads = {lane_head("plain", "one", ids[(2 * count) - 2], count), lane_head("plain", "two", ids.back(), count)};
    (void)on_sqlite(qiven3_ctx, p, Counts{2 * count, 0, count, 2});
}

QIVEN3_CASE(store_plain, lost_body) {
    constexpr std::array<std::string_view, 3> forgeries = {
        "DELETE FROM body WHERE id = 'a1'",                           // lost
        "UPDATE entry SET body = 0 WHERE id = 'a1'",                  // stray, the count short
        "UPDATE entry SET body = 1 - body WHERE id IN ('a1', 'a2')",  // one of each, the count equal
    };
    const auto site = std::make_unique<Site>();
    std::vector<std::byte> pages(page_bytes);
    Plan p;
    p.lines = {note("a1", "one", 1, {}, true, true), note("a2", "one", 2, "a1", false, false)};
    for (std::size_t i = 0; i < forgeries.size(); ++i) {
        const std::string path = text(site->root / std::to_string(i) / "store.db");
        Arena arena(pages);
        const auto s = std::make_unique<Store>(Store::open(path, true, false, site->fake));
        write(*s, p, arena);
        sqlite::Connection::open(String(path, Chars(arena)), false, test_at).exec(forgeries.at(i), test_at);
        const std::string denied = "StoreCorrupt incoherent [a1]";
        CHECK_EQ(said([&] { (void)scanned(*s, arena); }), denied);
        CHECK_EQ(said([&] { (void)exported(*s, arena); }), denied);
        CHECK_EQ(said([&] { (void)s->verify(); }), denied);
        (void)s->close();
        CHECK_EQ(said([&] { (void)Store::open(path, false, false, site->fake); }),
                 std::string("StoreCorrupt incoherent [body: the rows not the entries with a body key no forget "
                             "targets]"));
    }
}
