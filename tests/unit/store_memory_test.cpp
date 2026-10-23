// store/memory's smoke case, on the fake: the in-memory store meets the
// contract's concept; a content hit refused secret; an append of implementer's lane in two calls, the
// head, a header, a body record, forgotten, heads, size and a scan, whole
// and after a seq; a HeadConflict, nothing written; a second object over
// the same state sees the appends; a read-only one refuses append and
// serves head; NotFound; part 2's operations refused, not built; after
// close, StoreError closed.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
using qiven3::store::Header;
using qiven3::store::Heads;
using qiven3::store::LaneHead;
using qiven3::store::MemoryStore;
using qiven3::store::RangeView;
using qiven3::store::Written;
using qiven3::memory::String;
using qiven3::memory::Vector;
using Store = MemoryStore<Fake>;

static_assert(qiven3::store::Store<Store>, "the in-memory store meets the contract");
static_assert(!Store::capabilities.durable && !Store::capabilities.files && !Store::capabilities.processes,
              "the in-memory store declares no capability");

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::size_t state_bytes = std::size_t{8} << 20U;
constexpr std::size_t buffer_bytes = std::size_t{1} << 20U;

// The fake, the fixtures, the state's pages and the state, and the
// caller's arena.
struct Rig {
    Rig()
        : fake(qiven3::platform::fake::Config{}), book(fake), heap(fake.pages(state_bytes)),
          state(heap.bytes(), Limits{}), pages(buffer_bytes), arena(pages), chars(arena) {}
    Fake fake;
    Book book;
    qiven3::platform::fake::FakePages heap;
    qiven3::store::state::State state;
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
    CHECK_EQ(raised([&] { (void)store.verify(); }), std::string("StoreError internal"));

    CHECK(store.close().done);
    CHECK_EQ(raised([&] { (void)store.head(qiven_scope, implementer_lane); }), std::string("StoreError closed"));
    CHECK_EQ(again.size().volume.count, 3U);
}
