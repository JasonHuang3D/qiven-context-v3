// store's plain path, in both stores with no admit hook (NoAdmit), on the bed of
// plain_bed.hpp, over plain keys of lines no parser takes, each write followed by
// verify and, on SQLite, a reopen and verify again, then its own export imported
// into a fresh store of its type, verified (and reopened), and what each gives
// compared. store_plain_many_test.cpp holds the case of a large import.
// no_body_append, no_body_import: a record with no body commits and opens.
// forget_no_key_import, forget_rule_import: a forget whose target has no body
// key, is another scope's, is a forget or has an earlier forget, refused.
// id_held: an id held or repeated in a call, by append, forget and import,
// refused, the order's refusal first. keys_alike_import, keys_alike_append: a
// body, none, a body key forgotten, the same by a scan, an export and by id.
// lost_body: a body row lost or stray, the count
// equal or not, refused by a scan, an export and verify on the open store,
// then by open. body_by_id: body(id) of a body row lost refused, of no key,
// forgotten or a forget false, by a copy, a lent read and a transaction's view.
// sweep: a body (a record, of no bytes, none, a key or a record alone) by x1 (a
// note, of t1; a forget of none, of t1; a bare forget of t1, the body t1's), by
// append or forget, and by import. own_export: empty or not (a store's own
// export under an admit hook is the hook's to take). entry_bound: an entry at entry_bytes by
// append, import and forget committed and moved, a byte past refused.
// kind_bound: a kind past its bytes refused; its bytes counted, StoreFull.
// cross_scope_target, self_target: a target in another scope, or itself, held
// by append and by import alike, committed and moved (an admit hook's to
// refuse).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>  // pair
#include <vector>

#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "qiven/recordlog/contract.hpp"
#include "qiven/recordlog/errors.hpp"
#include "qiven/recordlog/sqlite_steps.hpp"
#include "qiven/recordlog/memory.hpp"
#include "qiven/recordlog/memory_state.hpp"
#include "qiven/recordlog/sqlite_store.hpp"
#include "qiven/platform/fake/fake_platform.hpp"
#include "support/stores/plain_bed.hpp"
#include "support/stores/records.hpp"

using namespace qiven::test::plain_bed;

namespace {

namespace records = qiven::test::records;

constexpr std::size_t state_bytes = std::size_t{8} << 20U;

// What body(id) says, the same by a copy, a lent read and a transaction's
// view: "held", "none" or its refusal.
std::string by_id(qiven::runner::Context& qiven_ctx, Store& s, std::string_view id, Arena& arena) {
    const auto read = [](const auto& f) {
        bool has = false;
        const std::string refused = said([&] { has = f(); });
        return refused.empty() ? std::string(has ? "held" : "none") : refused;
    };
    String record{Chars(arena)};
    sqlite::WriteView view(s.connection(), Chars(arena), nullptr, test_at, sqlite::Totals{});
    const auto lent = [](std::string_view, const std::string_view*, const sqlite::Chars&) {};
    const std::string copied = read([&] { return s.body(id, record); });
    CHECK_EQ(read([&] { return s.body(id, lent); }), copied);
    CHECK_EQ(read([&] { return view.body(id, record); }), copied);
    return copied;
}

// p on both stores, each store giving the same.
void on_both(qiven::runner::Context& qiven_ctx, const Plan& p, const Counts& counts) {
    const auto fake = std::make_unique<Fake>(qiven::platform::fake::Config{});
    auto heap = fake->pages(state_bytes);
    auto fresh = fake->pages(state_bytes);
    const auto st = std::make_unique<qiven::recordlog::state::State>(heap.bytes(), p.limits);
    const auto ft = std::make_unique<qiven::recordlog::state::State>(fresh.bytes(), records::suite_limits);
    qiven::recordlog::MemoryStore<Fake> s(*st, *fake);
    qiven::recordlog::MemoryStore<Fake> t(*ft, *fake);
    std::vector<std::byte> pages(page_bytes);
    Arena arena(pages);
    attempt(qiven_ctx, s, p, arena);
    CHECK(s.verify() == counts);
    const std::string gave = given(s, p, arena);
    moved(qiven_ctx, s, t, p, counts, gave);
    CHECK_EQ(gave, on_sqlite(qiven_ctx, p, counts));
}

}  // namespace

QIVEN_CASE(store_plain, no_body_append) {
    Plan p;
    p.lines = {note("n1", "one", 1, {}, false, false)};
    on_both(qiven_ctx, p, Counts{1, 0, 0, 1});
}

QIVEN_CASE(store_plain, no_body_import) {
    Plan p;
    p.import = true;
    p.lines = {note("n1", "one", 1, {}, false, false)};
    p.heads = {lane_head("plain", "one", "n1", 1)};
    on_both(qiven_ctx, p, Counts{1, 0, 0, 1});
}

QIVEN_CASE(store_plain, forget_no_key_import) {
    Plan p;
    p.import = true;
    p.lines = {note("t1", "one", 1, {}, false, false), gone("f1", "two", 1, {}, "t1")};
    p.heads = {lane_head("plain", "one", "t1", 1), lane_head("plain", "two", "f1", 1)};
    p.refused = "target 1 f1";
    on_both(qiven_ctx, p, Counts{});
}

QIVEN_CASE(store_plain, forget_rule_import) {
    const Given t1 = note("t1", "one", 1, {}, true, false);
    const Given f1 = gone("f1", "two", 1, {}, "t1");
    Given elsewhere = f1;
    elsewhere.keys.scope = "other";
    const std::array<std::pair<std::vector<Given>, std::string>, 4> rows = {{
        {{t1, f1, gone("f2", "two", 2, "f1", "t1")}, "target 2 f2"},  // a second forget
        {{t1, f1, gone("f2", "two", 2, "f1", "f1")}, "target 2 f2"},  // a forget of a forget
        {{t1, elsewhere}, "target 1 f1"},                             // another scope's
        {{gone("f1", "two", 1, {}, "f1")}, "target 0 f1"},            // its own target
    }};
    for (const auto& [lines, refused] : rows) {
        on_both(qiven_ctx, Plan{true, lines, {}, {}, {}, refused}, Counts{});
    }
}

QIVEN_CASE(store_plain, id_held) {
    const Given a1 = note("a1", "one", 1, {}, true, true);
    const Given a1_two = note("a1", "two", 1, {}, true, true);
    const Given a1_three = note("a1", "three", 1, {}, true, true);
    const Counts held{1, 1, 0, 1};
    on_both(qiven_ctx, Plan{false, {a1, a1_two}, {}, {}, {}, "id_held 0 a1"}, held);
    on_both(qiven_ctx, Plan{false, {a1, note("a1", "one", 1, "a1", true, true)}, {}, {}, {}, "seq 0 a1"}, held);
    on_both(qiven_ctx, Plan{false, {}, {}, {}, {a1, note("a1", "one", 2, "a1", true, true)}, "id_held 1 a1"}, {});
    on_both(qiven_ctx, Plan{false, {a1}, {}, gone("a1", "two", 1, {}, "a1"), {}, "id_held 0 a1"}, held);
    on_both(qiven_ctx, Plan{true, {a1, a1_two, a1_three}, {}, {}, {}, "id_held 1 a1"}, Counts{});
    on_both(qiven_ctx, Plan{true, {a1, a1}, {}, {}, {}, "parent 1 a1"}, Counts{});
}

QIVEN_CASE(store_plain, keys_alike_import) {
    Plan p;
    p.import = true;
    p.lines = {note("a1", "one", 1, {}, true, true), note("a2", "one", 2, "a1", false, false),
               note("a3", "one", 3, "a2", true, false), gone("f1", "two", 1, {}, "a3")};
    p.heads = {lane_head("plain", "one", "a3", 3), lane_head("plain", "two", "f1", 1)};
    on_both(qiven_ctx, p, Counts{4, 1, 1, 2});
}

QIVEN_CASE(store_plain, keys_alike_append) {
    Plan p;
    p.lines = {note("a1", "one", 1, {}, true, true), note("a2", "one", 2, "a1", false, false),
               note("a3", "one", 3, "a2", true, true)};
    p.forget = gone("f1", "two", 1, {}, "a3");
    on_both(qiven_ctx, p, Counts{4, 1, 1, 2});
}

QIVEN_CASE(store_plain, lost_body) {
    constexpr std::array<std::string_view, 3> forgeries = {
        "DELETE FROM body WHERE id = 'a1'",                           // lost
        "UPDATE entry SET body = 0 WHERE id = 'a1'",                  // stray, the count short
        "UPDATE entry SET body = 1 - body WHERE id IN ('a1', 'a2')",  // one of each, the count equal
    };
    const auto site = std::make_unique<Site>(Scratch{});
    std::vector<std::byte> pages(page_bytes);
    Plan p;
    p.lines = {note("a1", "one", 1, {}, true, true), note("a2", "one", 2, "a1", false, false)};
    for (std::size_t i = 0; i < forgeries.size(); ++i) {
        const std::string path = text(site->root / std::to_string(i) / "store.db");
        Arena arena(pages);
        const auto s = std::make_unique<Store>(opened(path, true, false, site->fake));
        write(*s, p, arena);
        sqlite::Connection::open(String(path, Chars(arena)), false, test_at).exec(forgeries.at(i), test_at);
        const std::string denied = "StoreCorrupt incoherent [a1]";
        CHECK_EQ(said([&] { (void)scanned(*s, arena); }), denied);
        CHECK_EQ(said([&] { (void)exported(*s, arena); }), denied);
        CHECK_EQ(said([&] { (void)s->verify(); }), denied);
        (void)s->close();
        CHECK_EQ(said([&] { (void)opened(path, false, false, site->fake); }),
                 std::string("StoreCorrupt incoherent [body: the rows not the entries with a body key no forget "
                             "targets]"));
    }
}

QIVEN_CASE(store_plain, body_by_id) {
    const auto site = std::make_unique<Site>(Scratch{});
    const std::string path = text(site->root / "store.db");
    std::vector<std::byte> pages(page_bytes);
    Arena arena(pages);
    Plan p;
    p.lines = {note("a1", "one", 1, {}, true, true), note("a2", "one", 2, "a1", false, false),
               note("a3", "one", 3, "a2", true, true)};
    p.forget = gone("f1", "one", 4, "a3", "a3");
    const Counts counts{4, 1, 1, 1};
    const auto rest = [&](Store& s) {  // no body key, forgotten, a forget
        for (const std::string_view id : {"a2", "a3", "f1"}) {
            CHECK_EQ(by_id(qiven_ctx, s, id, arena), std::string("none"));
        }
    };
    const auto s = std::make_unique<Store>(opened(path, true, false, site->fake));
    write(*s, p, arena);
    CHECK(s->verify() == counts);
    CHECK_EQ(by_id(qiven_ctx, *s, "a1", arena), std::string("held"));
    rest(*s);
    (void)s->close();
    const auto again = std::make_unique<Store>(opened(path, false, false, site->fake));
    CHECK(again->verify() == counts);
    rest(*again);
    sqlite::Connection::open(String(path, Chars(arena)), false, test_at).exec("DELETE FROM body WHERE id = 'a1'",
                                                                              test_at);
    const std::string denied = "StoreCorrupt incoherent [a1]";
    CHECK_EQ(by_id(qiven_ctx, *again, "a1", arena), denied);
    String line{Chars(arena)};
    again->header("a1", line);  // its header still given: the row is the body's
    CHECK(std::string_view(line) == "line a1");
    rest(*again);
    CHECK_EQ(said([&] { (void)again->verify(); }), denied);
    (void)again->close();
    CHECK(said([&] { (void)opened(path, false, false, site->fake); }).starts_with("StoreCorrupt incoherent"));
}

QIVEN_CASE(store_plain, sweep) {
    const std::array<std::pair<bool, std::optional<std::string>>, 5> bodies = {
        {{true, "record"}, {true, ""}, {false, {}}, {true, {}}, {false, "record"}}};
    const std::array<std::string_view, 2> ways = {"..TC. ..TC. ..T.T LLTCl MMMMm", "..TCf ..TCf ..TfT LLTC. MMMMm"};
    constexpr std::array codes = {"target", "forget_body", "body_missing", "body_mismatch", "forgotten_body"};
    for (std::size_t n = 0; n < 50; ++n) {
        const bool import = n % 2 == 1;
        const std::size_t k = (n / 2) % 5;
        const std::size_t at = std::string_view("TCLMFtclmf").find(ways.at(n % 2).at(((n / 10) * 6) + k));
        const std::string where = at >= 5 ? " 0 t1" : import ? " 1 x1" : " 0 x1";  // a lower case letter on t1
        const std::string refused = at > 9 ? std::string() : codes.at(at % 5) + where;  // a dot committed
        Given t = note("t1", "one", 1, {}, true, true);
        Given x = k < 2 ? note("x1", "two", 1, {}, true, true) : gone("x1", "two", 1, {}, "t1");
        std::tie(k == 4 ? t.keys.body : x.keys.body, k == 4 ? t.record : x.record) = bodies.at(n / 10);
        x.keys.target = k == 1 ? Parent("t1") : k == 2 ? Parent() : x.keys.target;
        const Plan p{import, import || k < 2 ? std::vector<Given>{t, x} : std::vector<Given>{t},
                     {lane_head("plain", "one", "t1", 1), lane_head("plain", "two", "x1", 1)},
                     !import && k >= 2 ? std::optional<Given>(x) : std::nullopt, {}, refused};
        const Counts committed = k >= 2 ? Counts{2, 0, 1, 2} : Counts{2, x.record ? 2U : 1U, 0, 2};
        const std::uint64_t held = import || at >= 5 ? 0 : 1;
        on_both(qiven_ctx, p, refused.empty() ? committed : Counts{held, t.record ? held : 0, 0, held});
    }
}

QIVEN_CASE(store_plain, own_export) {
    const Given a1 = note("a1", "one", 1, {}, true, true);
    on_both(qiven_ctx, Plan{false, {}, {}, {}, {}, {}, true}, Counts{});
    on_both(qiven_ctx, Plan{false, {a1}, {}, {}, {}, "StoreNotEmpty  []", true}, Counts{1, 1, 0, 1});
}

QIVEN_CASE(store_plain, entry_bound) {
    using qiven::recordlog::entry_bytes;
    for (const std::size_t past : {std::size_t{0}, std::size_t{1}}) {
        Given t = note("t1", "one", 1, {}, true, true);
        t.record = std::string(entry_bytes + past - 21, 'r');  // its line "line t1", its keys t1 plain one note
        Given f = gone("f1", "two", 1, {}, "t1");
        f.line.resize(entry_bytes + past - 18, 'l');  // its keys f1 plain two forget t1
        const Counts one = past == 0 ? Counts{1, 1, 0, 1} : Counts{};
        const std::string refused = past == 0 ? "" : "too_long 0 t1";
        on_both(qiven_ctx, Plan{false, {t}, {}, {}, {}, refused}, one);
        on_both(qiven_ctx, Plan{true, {t}, {lane_head("plain", "one", "t1", 1)}, {}, {}, refused}, one);
        const Given small = note("t1", "one", 1, {}, true, true);
        on_both(qiven_ctx, Plan{false, {small}, {}, f, {}, past == 0 ? "" : "too_long 0 f1"},
                past == 0 ? Counts{2, 0, 1, 2} : Counts{1, 1, 0, 1});
    }
}

QIVEN_CASE(store_plain, kind_bound) {
    const std::string kinds(qiven::recordlog::kind_bytes + 1, 'k');
    Given k = note("k1", "one", 1, {}, false, false);
    k.keys.kind = kinds;
    on_both(qiven_ctx, Plan{false, {k}, {}, {}, {}, "too_long 0 k1"}, Counts{});
    const qiven::recordlog::Limits limits{39, 39, 39, 10, 10, 10};  // "line k1" and its LF, and a kind of 31
    for (const std::size_t n : {std::size_t{31}, std::size_t{32}}) {
        k.keys.kind = std::string_view(kinds).substr(0, n);
        on_both(qiven_ctx, Plan{false, {k}, {}, {}, {}, n == 31 ? "" : "StoreFull  []", false, limits},
                n == 31 ? Counts{1, 0, 0, 1} : Counts{});
    }
}

QIVEN_CASE(store_plain, cross_scope_target) {
    Given t = note("t1", "one", 1, {}, false, false);
    t.keys.scope = "other";
    Given n = note("n2", "two", 1, {}, false, false);
    n.keys.target = "t1";
    const std::vector<LaneHead> heads = {lane_head("other", "one", "t1", 1), lane_head("plain", "two", "n2", 1)};
    on_both(qiven_ctx, Plan{false, {t, n}, {}, {}, {}, ""}, Counts{2, 0, 0, 2});
    on_both(qiven_ctx, Plan{true, {t, n}, heads, {}, {}, ""}, Counts{2, 0, 0, 2});
}

QIVEN_CASE(store_plain, self_target) {
    Given s = note("s1", "one", 1, {}, false, false);
    s.keys.target = "s1";
    on_both(qiven_ctx, Plan{false, {s}, {}, {}, {}, ""}, Counts{1, 0, 0, 1});
    on_both(qiven_ctx, Plan{true, {s}, {lane_head("plain", "one", "s1", 1)}, {}, {}, ""}, Counts{1, 0, 0, 1});
}
