// store/memory's smoke cases over plain records (records.hpp) with no admit
// hook, on the fake. smoke: the in-memory store meets the contract's
// concept; an append of
// implementer's lane in two calls, the head, a header, a body record,
// forgotten, heads, size and a scan, whole and after a seq; a
// HeadConflict, nothing written; a second object over the same state sees
// the appends; a read-only one refuses append and serves head; NotFound;
// verify's counts; after close, StoreError closed. exchange: forget_kind,
// the guard's two conflicts, a forget of B by E, its erased id, size and
// count; two records forgotten in one call; an export opened
// before the forget yields B's body, not forgotten; an export
// imported into an empty state, equal; StoreNotEmpty; an import refused
// forgotten_body writing nothing; an export closed by its store's close.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/memory.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/stores/records.hpp"

namespace {

using namespace qiven3::test::records;  // NOLINT(google-build-using-namespace)
using Fake = qiven3::platform::fake::FakePlatform;
using Arena = qiven3::memory::Arena;
using Chars = qiven3::memory::Allocator<char>;
using qiven3::store::Counts;
using qiven3::store::EntryView;
using qiven3::store::GuardView;
using qiven3::store::Heads;
using qiven3::store::Limits;
using qiven3::store::Operation;
using qiven3::store::Record;
using qiven3::store::Id;
using qiven3::store::Imported;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::MemoryStore;
using qiven3::store::RangeView;
using qiven3::store::Written;
using qiven3::memory::String;
using qiven3::memory::Vector;
using Store = MemoryStore<Fake>;
using Recs = std::vector<Rec>;

static_assert(qiven3::store::Store<Store>, "the in-memory store meets the contract");
static_assert(!Store::capabilities.durable && !Store::capabilities.files && !Store::capabilities.processes,
              "the in-memory store declares no capability");

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::string_view reviewer_lane = "reviewer";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;
constexpr std::size_t buffer_bytes = std::size_t{1} << 20U;

// The fake, implementer's lane of A, B, D and ACC, the state's pages and
// the state, and the caller's arena.
struct Rig {
    Rig()
        : fake(qiven3::platform::fake::Config{}), lane(chain(qiven_scope, implementer_lane, {"A", "B", "D", "ACC"})),
          heap(fake.pages(state_bytes)), state(heap.bytes(), Limits{}), heap2(fake.pages(state_bytes)),
          state2(heap2.bytes(), Limits{}), pages(buffer_bytes), arena(pages), chars(arena) {}
    Fake fake;
    Recs lane;
    qiven3::platform::fake::FakePages heap;
    qiven3::store::state::State state;
    qiven3::platform::fake::FakePages heap2;
    qiven3::store::state::State state2;  // an import's
    std::vector<std::byte> pages;
    Arena arena;
    Chars chars;
};

// The records a scan gives, by id.
std::vector<std::string> scanned(Store& store, const RangeView& range, const Chars& chars) {
    std::vector<std::string> ids;
    auto scan = store.scan(range);
    Record r(chars);
    while (scan.next(r)) {
        ids.emplace_back(r.id.view());
    }
    return ids;
}

// The error a call raised, as "Kind code", or "none".
template <class F>
std::string raised(const F& f) {
    const std::optional<Refusal> r = refused(f);
    return r ? std::string(qiven3::store::name(r->kind)) + " " + r->code : std::string("none");
}

// A and B appended, D after them; a HeadConflict between, nothing written.
void appends(qiven3::test::Context& qiven3_ctx, Store& store, const Recs& lane) {
    CHECK_EQ(store.size().volume.count, 0U);
    const Recs first = {lane[0], lane[1]};
    const std::vector<EntryView> lines = views(first);
    const Written w = store.append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(lines));
    CHECK_EQ(w.head.id.view(), id_of(first[1]));
    CHECK_EQ(w.head.seq, 2);
    CHECK_EQ(w.volume.count, 2U);
    CHECK_EQ(w.volume.size, added(first));
    CHECK(w.purge.done);
    const Recs second = {lane[2]};
    const std::vector<EntryView> more = views(second);
    CHECK(raised([&] {
              (void)store.append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(more));
          }).starts_with("HeadConflict"));
    CHECK(raised([&] {
              (void)store.append(qiven_scope, implementer_lane, id_of(first[0]), std::span<const EntryView>(more));
          }).starts_with("HeadConflict"));
    CHECK_EQ(store.size().volume.count, 2U);
    const Written w2 = store.append(qiven_scope, implementer_lane, id_of(first[1]), std::span<const EntryView>(more));
    CHECK_EQ(w2.head.seq, 3);
    CHECK_EQ(w2.volume.count, 3U);
    CHECK_EQ(w2.volume.size, added(first) + added(second));
}

// The reads over A, B and D.
void reads(qiven3::test::Context& qiven3_ctx, Store& store, const Recs& lane, const Chars& chars) {
    const Rec& a = lane[0];
    const Rec& d = lane[2];
    const std::optional<qiven3::store::Head> head = store.head(qiven_scope, implementer_lane);
    REQUIRE(head.has_value());
    CHECK_EQ(head->id.view(), id_of(d));
    CHECK(!store.head(qiven_scope, "reviewer").has_value());
    String h(chars);
    store.header(id_of(a), h);
    CHECK_EQ(std::string_view(h), a.line);  // A's line, as given
    String record(chars);
    CHECK_EQ(store.body(id_of(a), record), a.record.has_value());
    if (a.record) {
        CHECK_EQ(std::string_view(record), std::string_view(*a.record));
    }
    CHECK(!store.forgotten(id_of(a)));
    const std::string absent = "sha256:" + std::string(64, '0');
    CHECK(raised([&] { store.header(absent, h); }).starts_with("NotFound"));
    Vector<LaneHead> rows(chars);
    const Heads hs = store.heads(std::nullopt, rows);
    REQUIRE(rows.size() == 1U);
    CHECK_EQ(rows[0].head.id.view(), id_of(d));
    CHECK_EQ(rows[0].scope.view(), qiven_scope);
    CHECK_EQ(hs.forgets, 0U);
    const std::array<std::string_view, 1> star = {"project:*"};
    const std::array<std::string_view, 1> elsewhere = {"project:other"};
    (void)store.heads(std::span<const std::string_view>(star), rows);
    CHECK_EQ(rows.size(), 1U);
    (void)store.heads(std::span<const std::string_view>(elsewhere), rows);
    CHECK(rows.empty());
    CHECK_EQ(store.size().figures, 0U);
    const std::vector<std::string> all = scanned(store, RangeView{}, chars);
    REQUIRE(all.size() == 3U);
    CHECK_EQ(all[0], id_of(a));
    CHECK_EQ(all[2], id_of(d));
    RangeView after;
    after.scope = qiven_scope;
    after.after = 2;
    const std::vector<std::string> tail = scanned(store, after, chars);
    REQUIRE(tail.size() == 1U);
    CHECK_EQ(tail[0], id_of(d));
}

}  // namespace

QIVEN3_CASE(store_memory, smoke) {
    const auto rig = std::make_unique<Rig>();
    Store store(rig->state, rig->fake);
    CHECK(!store.read_only());
    CHECK(store.limits() == Limits{});
    appends(qiven3_ctx, store, rig->lane);
    reads(qiven3_ctx, store, rig->lane, rig->chars);

    // A second object over the state, and a read-only one.
    Store again(rig->state, rig->fake);
    CHECK_EQ(again.head(qiven_scope, implementer_lane)->seq, 3);
    Store reader(rig->state, rig->fake, true);
    CHECK(reader.read_only());
    CHECK_EQ(reader.head(qiven_scope, implementer_lane)->seq, 3);
    CHECK_EQ(raised([&] { (void)reader.append(qiven_scope, implementer_lane, std::nullopt, {}); }),
             std::string("StoreError read_only"));
    const Counts counts = store.verify();
    CHECK_EQ(counts.entries, 3U);
    CHECK_EQ(counts.bodies, 3U);
    CHECK_EQ(counts.forgets, 0U);
    CHECK_EQ(counts.lanes, 1U);

    CHECK(store.close().done);
    CHECK_EQ(raised([&] { (void)store.head(qiven_scope, implementer_lane); }), std::string("StoreError closed"));
    CHECK_EQ(again.size().volume.count, 3U);
}

namespace {

// The guard: every head the store holds, its views into rows.
std::vector<GuardView> guard_of(Store& store, Vector<LaneHead>& rows) {
    (void)store.heads(std::nullopt, rows);
    std::vector<GuardView> out;
    for (const LaneHead& h : rows) {
        out.push_back(GuardView{h.scope.view(), h.lane.view(), h.head.id.view()});
    }
    return out;
}

// A source of records with their keys and the heads given.
struct ListSource {
    std::vector<LaneHead> given;
    Recs lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        const Rec& s = lines[at++];
        line.header.assign(s.line);
        line.has_body = s.record.has_value();
        line.body.assign(s.record.value_or(std::string()));
        line.forgotten = false;
        return line.keys.assign(view(s.keys));
    }
};

// E: a forget of B on implementer's lane after ACC.
Rec forget_e() { return forget("E", qiven_scope, implementer_lane, 5, "ACC", "B"); }

// forget_kind, the guard's conflicts, then B forgotten by E.
void forgets(qiven3::test::Context& qiven3_ctx, Store& store, const Recs& lane, const Chars& chars) {
    const Rec& b = lane[1];
    const Rec e = forget_e();
    const Recs kind = {lane[0]};
    const Recs call = {e};
    const std::vector<EntryView> wrong = views(kind);
    const std::vector<EntryView> lines = views(call);
    Vector<LaneHead> rows(chars);
    std::vector<GuardView> guard = guard_of(store, rows);
    const std::string head(rows[0].head.id.view());
    Vector<Id> erased(chars);
    const auto forget = [&](const std::vector<EntryView>& entries) {
        return store.forget(qiven_scope, implementer_lane, head, entries, guard, erased);
    };
    CHECK_EQ(raised([&] { (void)forget(wrong); }), std::string("StoreError forget_kind"));
    const std::vector<GuardView> full = guard;
    guard.clear();
    CHECK(raised([&] { (void)forget(lines); }).starts_with("HeadConflict"));
    guard = full;
    guard.push_back(GuardView{qiven_scope, "learner", head});
    CHECK(raised([&] { (void)forget(lines); }).starts_with("HeadConflict"));
    guard = full;
    const std::uint64_t size = store.size().volume.size;
    const Written w = forget(lines);
    CHECK_EQ(w.head.id.view(), id_of(e));
    CHECK_EQ(w.volume.count, 5U);
    CHECK_EQ(w.volume.size, size - (b.record->size() + 1U) + e.line.size() + 1U);
    REQUIRE(erased.size() == 1U);
    CHECK_EQ(erased[0].view(), id_of(b));
    CHECK(store.forgotten(id_of(b)));
    String record(chars);
    CHECK(!store.body(id_of(b), record));
}

// Reviewer's lane of C, M1 and M2; then M2 and M1 forgotten in one call
// (a forget of M1 alone refused whole_record is checked's case).
void records(qiven3::test::Context& qiven3_ctx, Store& store, const Chars& chars) {
    const Recs lane = chain(qiven_scope, reviewer_lane, {"C", "M1", "M2"});
    const std::vector<EntryView> held = views(lane);
    (void)store.append(qiven_scope, reviewer_lane, std::nullopt, held);
    const Rec f2 = forget("F2", qiven_scope, reviewer_lane, 4, "M2", "M2");
    const Rec f3 = forget("F3", qiven_scope, reviewer_lane, 5, "F2", "M1");
    Vector<LaneHead> rows(chars);
    const std::vector<GuardView> guard = guard_of(store, rows);
    Vector<Id> erased(chars);
    CHECK_EQ(store.size().volume.count, 8U);
    const Recs whole = {f2, f3};
    const std::vector<EntryView> whole_lines = views(whole);
    const Written w = store.forget(qiven_scope, reviewer_lane, id_of(lane[2]), whole_lines, guard, erased);
    CHECK_EQ(w.head.seq, 5);
    CHECK_EQ(erased.size(), 2U);
    CHECK(store.forgotten(id_of(lane[1])) && store.forgotten(id_of(lane[2])));
}

// An export's lines, as ids; B's body and forgotten flag kept apart.
struct Read {
    std::vector<std::string> ids;
    bool b_body = false;
    bool b_forgotten = false;
};
// Each line on a fresh arena, its id by its keys.
using Exported = qiven3::store::state::MemoryExport;
Read read_all(Exported& x, const Rec& b) {
    Read out;
    std::vector<std::byte> pages(std::size_t{1} << 18U);
    for (;;) {
        Arena arena(pages);
        Line line(arena);
        if (!x.next(line)) {
            return out;
        }
        const std::string id(line.keys.id.view());
        if (id == id_of(b)) {
            out.b_body = line.has_body && std::string_view(line.body) == *b.record;
            out.b_forgotten = line.forgotten;
        }
        out.ids.push_back(id);
    }
}

// The bodies a state keeps for an open export: forgotten, not yet freed.
std::size_t kept(qiven3::store::state::State& s) {
    const qiven3::store::state::Locked lock(s, Operation::size, {});
    const auto& held = lock.data().held;
    return static_cast<std::size_t>(
        std::count_if(held.begin(), held.end(), [](const auto& e) { return !e.has_body && !e.body.empty(); }));
}

// An export's lines, counted.
std::size_t lines_of(Exported& x) {
    std::vector<std::byte> pages(std::size_t{1} << 18U);
    for (std::size_t n = 0;; ++n) {
        Arena arena(pages);
        Line line(arena);
        if (!x.next(line)) {
            return n;
        }
    }
}

}  // namespace

QIVEN3_CASE(store_memory, exchange) {
    const auto rig = std::make_unique<Rig>();
    const Recs& lane = rig->lane;
    Store store(rig->state, rig->fake);
    appends(qiven3_ctx, store, lane);
    const Recs acc = {lane[3]};
    const std::vector<EntryView> acc_lines = views(acc);
    {
        // A scan made before ACC's append gives the three entries held then.
        auto scan = store.scan(RangeView{});
        Record h(rig->chars);
        std::size_t n = scan.next(h) ? 1U : 0U;
        (void)store.append(qiven_scope, implementer_lane, id_of(lane[2]), acc_lines);
        while (scan.next(h)) {
            ++n;
        }
        CHECK_EQ(n, 3U);
    }
    auto early = store.export_(std::nullopt, {});
    forgets(qiven3_ctx, store, lane, rig->chars);
    records(qiven3_ctx, store, rig->chars);

    // The export opened before the forget: B's body, not forgotten.
    Vector<LaneHead> rows(rig->chars);
    CHECK_EQ(early.heads(rows).forgets, 0U);
    CHECK_EQ(rows.size(), 1U);
    const Read before = read_all(early, lane[1]);
    CHECK_EQ(before.ids.size(), 4U);
    CHECK(before.b_body && !before.b_forgotten);
    CHECK_EQ(kept(rig->state), 3U);
    const std::array<std::string_view, 1> star = {"project:*"};
    CHECK_EQ(store.heads(std::span<const std::string_view>(star), rows).forgets, 3U);
    early.close();
    CHECK_EQ(kept(rig->state), 0U);
    CHECK(raised([&] { (void)early.heads(rows); }).starts_with("StoreError closed"));

    // The whole store exported and imported into an empty state.
    const Counts counts = store.verify();
    CHECK_EQ(counts.entries, 10U);
    CHECK_EQ(counts.forgets, 3U);
    CHECK_EQ(counts.lanes, 2U);
    Store other(rig->state2, rig->fake);
    ListSource bad;
    bad.lines = {lane[0], lane[1], lane[2], lane[3], forget_e()};
    // Pushed, not assigned from a list: g++ 13 under the sanitizers takes
    // the list's copy for a read past its one head (-Warray-bounds).
    bad.given.push_back(lane_head(qiven_scope, implementer_lane, id_of(bad.lines.back()), 5));
    Vector<LaneHead> wrote(rig->chars);
    const std::optional<Refusal> refusal = refused([&] { (void)other.import_(bad, wrote); });
    REQUIRE(refusal.has_value());
    CHECK_EQ(refusal->code, std::string("forgotten_body"));
    CHECK_EQ(other.size().volume.count, 0U);
    auto whole = store.export_(std::nullopt, {});
    const Imported in = other.import_(whole, wrote);
    CHECK(in.counts == counts);
    CHECK_EQ(in.volume.size, store.size().volume.size);
    CHECK_EQ(wrote.size(), 2U);
    CHECK(other.verify() == counts);
    CHECK(other.forgotten(id_of(lane[1])));
    const std::array<qiven3::store::AfterView, 1> from = {qiven3::store::AfterView{qiven_scope, implementer_lane, 2}};
    auto tail = store.export_(std::nullopt, from);
    CHECK_EQ(lines_of(tail), counts.entries - 2U);
    tail.close();
    auto again = store.export_(std::nullopt, {});
    CHECK(raised([&] { (void)other.import_(again, wrote); }).starts_with("StoreNotEmpty"));
    CHECK(store.close().done);
    CHECK(raised([&] { (void)again.heads(rows); }).starts_with("StoreError closed"));
}
