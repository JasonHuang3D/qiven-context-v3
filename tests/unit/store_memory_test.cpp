// store/memory's smoke cases, on the fake. smoke: the in-memory store
// meets the contract's concept; a content hit refused secret; an append of
// implementer's lane in two calls, the head, a header, a body record,
// forgotten, heads, size and a scan, whole and after a seq; a
// HeadConflict, nothing written; a second object over the same state sees
// the appends; a read-only one refuses append and serves head; NotFound;
// verify's counts; after close, StoreError closed. exchange: forget_kind,
// the guard's two conflicts, a forget of B by E, its erased id, size and
// count; whole_record, then the record forgotten whole; an export opened
// before the forget yields B's body, not forgotten; an export
// imported into an empty state, equal; StoreNotEmpty; an import refused
// forgotten_body writing nothing; an export closed by its store's close.
// cleared: a content hit refused with no direction record clearing it,
// or one clearing another pattern; admitted on a state an import filled
// from one holding the clearing, then refused there once used; refused
// once the clearing is forgotten.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "checked/stores.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/memory.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
using qiven3::store::Counts;
using qiven3::store::GuardView;
using qiven3::store::Header;
using qiven3::store::Heads;
using qiven3::store::Id;
using qiven3::store::Imported;
using qiven3::store::LaneHead;
using qiven3::store::Line;
using qiven3::store::MemoryStore;
using qiven3::store::RangeView;
using qiven3::store::Written;
using qiven3::memory::String;
using qiven3::memory::Vector;
using Store = qiven3::checked::MemoryStore<Fake>;

static_assert(qiven3::store::Store<Store>, "the in-memory store meets the contract");
static_assert(!Store::capabilities.durable && !Store::capabilities.files && !Store::capabilities.processes,
              "the in-memory store declares no capability");

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::string_view reviewer_lane = "reviewer";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;
constexpr std::size_t buffer_bytes = std::size_t{1} << 20U;

// The fake, the fixtures, the state's pages and the state, and the
// caller's arena.
struct Rig {
    Rig()
        : fake(qiven3::platform::fake::Config{}), book(fake), heap(fake.pages(state_bytes)),
          state(heap.bytes(), Limits{}), heap2(fake.pages(state_bytes)), state2(heap2.bytes(), Limits{}),
          pages(buffer_bytes), arena(pages), chars(arena) {}
    Fake fake;
    Book book;
    qiven3::platform::fake::FakePages heap;
    qiven3::store::state::State state;
    qiven3::platform::fake::FakePages heap2;
    qiven3::store::state::State state2;  // an import's
    std::vector<std::byte> pages;
    Arena arena;
    Chars chars;
};

// The headers a scan gives, by id.
std::vector<std::string> scanned(Store& store, const RangeView& range, const Chars& chars) {
    std::vector<std::string> ids;
    auto scan = store.scan(range);
    Header h(chars);
    while (scan.next(h)) {
        ids.emplace_back(h.value(Slot::id));
    }
    return ids;
}

// The error a call raised, as "Kind code", or "none".
template <class F>
std::string raised(const F& f) {
    const std::optional<Refusal> r = refused(f);
    return r ? std::string(qiven3::store::name(r->kind)) + " " + r->code : std::string("none");
}

// A content hit refused, then A and B appended, D after them; a
// HeadConflict between, nothing written.
void appends(qiven3::test::Context& qiven3_ctx, Store& store, const Book& book) {
    const std::vector<Stored> hit = {book.with_secret("A", std::nullopt)};
    const std::vector<EntryView> hit_lines = views(hit);
    CHECK_EQ(raised([&] {
                 (void)store.append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(hit_lines));
             }),
             std::string("EntryInvalid secret"));
    CHECK_EQ(store.size().volume.count, 0U);
    const std::vector<Stored> first = {book.stored("A"), book.stored("B")};
    const std::vector<EntryView> lines = views(first);
    const Written w = store.append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(lines));
    CHECK_EQ(w.head.id.view(), id_of(first[1]));
    CHECK_EQ(w.head.seq, 2);
    CHECK_EQ(w.volume.count, 2U);
    CHECK_EQ(w.volume.size, added(first));
    CHECK(w.purge.done);
    const std::vector<Stored> second = {book.stored("D")};
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
void reads(qiven3::test::Context& qiven3_ctx, Store& store, const Book& book, const Chars& chars) {
    const Stored a = book.stored("A");
    const Stored d = book.stored("D");
    const std::optional<qiven3::store::Head> head = store.head(qiven_scope, implementer_lane);
    REQUIRE(head.has_value());
    CHECK_EQ(head->id.view(), id_of(d));
    CHECK(!store.head(qiven_scope, "reviewer").has_value());
    Header h(chars);
    store.header(id_of(a), h);
    CHECK_EQ(h.value(Slot::id), id_of(a));
    CHECK_EQ(h.seq(), 1);
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
    appends(qiven3_ctx, store, rig->book);
    reads(qiven3_ctx, store, rig->book, rig->chars);

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

// A forget of target as like's (E's) fields give it, on lane at seq after
// parent, its writer's role the lane's, at the time given.
Stored forget_of(const Stored& like, const Stored& target, const Stored& parent, std::string_view lane,
                 std::int64_t seq, std::string_view at) {
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
    f.at = at;
    f.target = target_id;
    Stored s{std::string(), std::nullopt, std::nullopt};
    header::make(f).line(s.line);
    return s;
}

// The guard: every head the store holds, its views into rows.
std::vector<GuardView> guard_of(Store& store, Vector<LaneHead>& rows) {
    (void)store.heads(std::nullopt, rows);
    std::vector<GuardView> out;
    for (const LaneHead& h : rows) {
        out.push_back(GuardView{h.scope.view(), h.lane.view(), h.head.id.view()});
    }
    return out;
}

// A source of stored lines with the heads given.
struct ListSource {
    std::vector<LaneHead> given;
    std::vector<Stored> lines;
    std::size_t at = 0;
    void heads(Vector<LaneHead>& rows) const { rows.assign(given.begin(), given.end()); }
    bool next(Line& line) {
        if (at == lines.size()) {
            return false;
        }
        const Stored& s = lines[at++];
        line.header.assign(s.line);
        line.has_body = s.record.has_value();
        line.body.assign(s.record.value_or(std::string()));
        line.forgotten = false;
        return true;
    }
};

// forget_kind, the guard's conflicts, then B forgotten by E.
void forgets(qiven3::test::Context& qiven3_ctx, Store& store, const Book& book, const Chars& chars) {
    const Stored b = book.stored("B");
    const Stored e = book.stored("E");
    const std::vector<Stored> kind = {book.stored("A")};
    const std::vector<Stored> call = {e};
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

// A forget of M1 alone refused whole_record, M2 revising it; then M2 and
// M1 forgotten together.
void records(qiven3::test::Context& qiven3_ctx, Store& store, const Book& book, const Chars& chars) {
    const std::vector<Stored> lane = {book.stored("C"), book.stored("M1"), book.stored("M2")};
    const std::vector<EntryView> held = views(lane);
    (void)store.append(qiven_scope, reviewer_lane, std::nullopt, held);
    const Stored e = book.stored("E");
    const std::string_view later = "2026-10-17T13:00:00.000Z";  // after M2's
    const Stored f1 = forget_of(e, lane[1], lane[2], reviewer_lane, 4, later);
    const Stored f2 = forget_of(e, lane[2], lane[2], reviewer_lane, 4, later);
    const Stored f3 = forget_of(e, lane[1], f2, reviewer_lane, 5, later);
    Vector<LaneHead> rows(chars);
    const std::vector<GuardView> guard = guard_of(store, rows);
    Vector<Id> erased(chars);
    const std::vector<Stored> part = {f1};
    const std::vector<EntryView> part_lines = views(part);
    const std::optional<Refusal> r = refused([&] {
        (void)store.forget(qiven_scope, reviewer_lane, id_of(lane[2]), part_lines, guard, erased);
    });
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("whole_record"));
    CHECK_EQ(r->detail, std::string("entry 0"));
    CHECK_EQ(store.size().volume.count, 8U);
    const std::vector<Stored> whole = {f2, f3};
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
Read read_all(qiven3::store::state::MemoryExport& x, const Stored& b, Arena& arena) {
    Read out;
    Line line(arena);
    while (x.next(line)) {
        const std::string id(header::parse_line(std::string_view(line.header)).value(Slot::id));
        if (id == id_of(b)) {
            out.b_body = line.has_body && std::string_view(line.body) == *b.record;
            out.b_forgotten = line.forgotten;
        }
        out.ids.push_back(id);
    }
    return out;
}

// The bodies a state keeps for an open export: forgotten, not yet freed.
std::size_t kept(qiven3::store::state::State& s) {
    const qiven3::store::state::Locked lock(s, Operation::size, {});
    const auto& held = lock.data().held;
    return static_cast<std::size_t>(
        std::count_if(held.begin(), held.end(), [](const auto& e) { return !e.has_body && !e.body.empty(); }));
}

// An export's lines, counted.
std::size_t lines_of(qiven3::store::state::MemoryExport& x, Arena& arena) {
    Line line(arena);
    std::size_t n = 0;
    while (x.next(line)) {
        ++n;
    }
    return n;
}

}  // namespace

QIVEN3_CASE(store_memory, exchange) {
    const auto rig = std::make_unique<Rig>();
    const Book& book = rig->book;
    Store store(rig->state, rig->fake);
    appends(qiven3_ctx, store, book);
    const std::vector<Stored> acc = {book.stored("ACC")};
    const std::vector<EntryView> acc_lines = views(acc);
    {
        // A scan made before ACC's append gives the three entries held then.
        auto scan = store.scan(RangeView{});
        Header h(rig->chars);
        std::size_t n = scan.next(h) ? 1U : 0U;
        (void)store.append(qiven_scope, implementer_lane, id_of(book.stored("D")), acc_lines);
        while (scan.next(h)) {
            ++n;
        }
        CHECK_EQ(n, 3U);
    }
    auto early = store.export_(std::nullopt, {});
    forgets(qiven3_ctx, store, book, rig->chars);
    records(qiven3_ctx, store, book, rig->chars);

    // The export opened before the forget: B's body, not forgotten.
    Vector<LaneHead> rows(rig->chars);
    CHECK_EQ(early.heads(rows).forgets, 0U);
    CHECK_EQ(rows.size(), 1U);
    const Read before = read_all(early, book.stored("B"), rig->arena);
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
    bad.lines = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC"), book.stored("E")};
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
    CHECK(other.forgotten(id_of(book.stored("B"))));
    const std::array<qiven3::store::AfterView, 1> from = {qiven3::store::AfterView{qiven_scope, implementer_lane, 2}};
    auto tail = store.export_(std::nullopt, from);
    CHECK_EQ(lines_of(tail, rig->arena), counts.entries - 2U);
    tail.close();
    auto again = store.export_(std::nullopt, {});
    CHECK(raised([&] { (void)other.import_(again, wrote); }).starts_with("StoreNotEmpty"));
    CHECK(store.close().done);
    CHECK(raised([&] { (void)again.heads(rows); }).starts_with("StoreError closed"));
}

namespace {

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
        writer = std::string(reviewer_lane) + std::string(f.writer.substr(f.writer.find('/')));
        f.lane = reviewer_lane;
        f.writer = writer;
        f.parent = parent == nullptr ? std::nullopt : std::optional<std::string_view>(parent_id);
        f.target = target == nullptr ? std::nullopt : std::optional<std::string_view>(target_id);
        f.seq = seq;
        f.at = at;
    });
}

}  // namespace

// A false secret hit's clearing on held direction records, on the fake.
QIVEN3_CASE(store_memory, cleared) {
    const auto rig = std::make_unique<Rig>();
    const Book& book = rig->book;
    Store store(rig->state, rig->fake);
    // A's line with the aws-akia key, at noon: past every record below.
    const std::vector<Stored> hit = {book.made("A", "A", *book.with_secret("A", std::nullopt).content,
                                               [](header::FieldsView& f) { f.at = "2026-10-17T12:00:00.000Z"; })};
    const std::vector<EntryView> hit_lines = views(hit);
    const auto admit = [&hit_lines](Store& s, qiven3::store::Expected expected) {
        return raised([&] { (void)s.append(qiven_scope, implementer_lane, expected, hit_lines); });
    };
    const std::string content = content_of(hit[0]);
    const std::vector<Stored> other = {
        clearing(book, content, "sk-key", nullptr, nullptr, 1, "2026-10-17T09:00:00.000Z")};
    const std::vector<Stored> clear = {
        clearing(book, content, "aws-akia", &other[0], nullptr, 2, "2026-10-17T09:10:00.000Z")};
    const std::vector<EntryView> other_lines = views(other);
    const std::vector<EntryView> clear_lines = views(clear);
    CHECK_EQ(admit(store, std::nullopt), std::string("EntryInvalid secret"));
    (void)store.append(qiven_scope, reviewer_lane, std::nullopt, other_lines);
    CHECK_EQ(admit(store, std::nullopt), std::string("EntryInvalid secret"));
    (void)store.append(qiven_scope, reviewer_lane, id_of(other[0]), clear_lines);

    // The state imported into an empty one: the clearing held there.
    Store into(rig->state2, rig->fake);
    {
        auto x = store.export_(std::nullopt, {});
        Vector<LaneHead> wrote(rig->chars);
        (void)into.import_(x, wrote);
        x.close();
    }

    // The clearing forgotten: the hit refused again.
    const std::vector<Stored> gone = {
        forget_of(book.stored("E"), clear[0], clear[0], reviewer_lane, 3, "2026-10-17T11:00:00.000Z")};
    const std::vector<EntryView> gone_lines = views(gone);
    Vector<LaneHead> rows(rig->chars);
    const std::vector<GuardView> guard = guard_of(store, rows);
    Vector<Id> erased(rig->chars);
    (void)store.forget(qiven_scope, reviewer_lane, id_of(clear[0]), gone_lines, guard, erased);
    CHECK_EQ(erased.size(), 1U);
    CHECK_EQ(admit(store, std::nullopt), std::string("EntryInvalid secret"));

    // The other clearing revised for aws-akia, then for sk-key again: its
    // last revision stands, the hit refused.
    std::vector<Stored> revised = {
        clearing(book, content, "aws-akia", &gone[0], &other[0], 4, "2026-10-17T11:10:00.000Z")};
    revised.push_back(clearing(book, content, "sk-key", &revised[0], &other[0], 5, "2026-10-17T11:20:00.000Z"));
    const std::vector<EntryView> revised_lines = views(revised);
    CHECK_EQ(raised([&] { (void)store.append(qiven_scope, reviewer_lane, id_of(gone[0]), revised_lines); }),
             std::string("none"));
    CHECK_EQ(admit(store, std::nullopt), std::string("EntryInvalid secret"));

    // On the imported state, cleared: admitted; then, its content held,
    // refused.
    CHECK_EQ(admit(into, std::nullopt), std::string("none"));
    CHECK_EQ(into.verify().entries, 3U);
    CHECK_EQ(admit(into, id_of(hit[0])), std::string("EntryInvalid secret"));
}

// A false secret hit's clearing over many direction records: each root's work given back
// before the next, the append's counted peak the same for 4 records held
// as for 16, within its budget.
QIVEN3_CASE(store_memory, cleared_many) {
    const auto rig = std::make_unique<Rig>();
    const Book& book = rig->book;
    const std::vector<Stored> hit = {book.made("A", "A", *book.with_secret("A", std::nullopt).content,
                                               [](header::FieldsView& f) { f.at = "2026-10-17T12:00:00.000Z"; })};
    const std::vector<EntryView> hit_lines = views(hit);
    const std::string content = content_of(hit[0]);
    // n records on reviewer's lane, n - 1 roots clearing sk-key, then a
    // revision of the last root clearing aws-akia: every root folded, the
    // last with its revision, before the hit passes.
    const auto peak_with = [&](std::size_t n, auto& state) {
        Store store(state, rig->fake);
        std::vector<Stored> records;
        records.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            const std::string at = "2026-10-17T09:" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ":00.000Z";
            const Stored* parent = records.empty() ? nullptr : &records.back();
            const bool last = i + 1 == n;
            records.push_back(clearing(book, content, last ? "aws-akia" : "sk-key", parent,
                                       last ? parent : nullptr, static_cast<std::int64_t>(i + 1), at));
        }
        const std::vector<EntryView> lines = views(records);
        CHECK_EQ(raised([&] { (void)store.append(qiven_scope, reviewer_lane, std::nullopt, lines); }),
                 std::string("none"));
        CHECK_EQ(raised([&] { (void)store.append(qiven_scope, implementer_lane, std::nullopt, hit_lines); }),
                 std::string("none"));
        return store.peak();
    };
    const std::size_t four = peak_with(4, rig->state);
    const std::size_t sixteen = peak_with(16, rig->state2);
    std::cout << "store_memory.cleared_many: append " << four << " bytes counted with 4 records, " << sixteen
              << " with 16, of " << qiven3::store::state::budget_of(hit_lines) << '\n';
    CHECK_EQ(four, sixteen);
    CHECK(sixteen <= qiven3::store::state::budget_of(hit_lines));
}
