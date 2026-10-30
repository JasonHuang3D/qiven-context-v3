// checked's rules through its ruled stores, the store's cases that test a
// rule of the entry's, moved from them, on the fake. widths: the store's
// fields against the entry's (an id, a field's path, a scope and a lane at
// their syntaxes' longest; every pattern's name and every validator code
// whole). secret_hit: a content hit refused secret at append, nothing
// written. whole_record: a forget of M1 alone refused whole_record, M2
// revising it; then M2 and M1 forgotten together. own_export: a ruled
// store's own export imported, into itself empty, refused StoreNotEmpty
// once it holds an entry, in memory and in SQLite. Each write followed by
// verify and, on SQLite, a reopen and verify again.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "entry/content_fields.hpp"
#include "entry/header.hpp"
#include "entry/secret.hpp"
#include "entry/validator.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/memory.hpp"
#include "support/checked/stores.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
using qiven3::memory::Vector;
using qiven3::store::Counts;
using qiven3::store::GuardView;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::store::Written;
using Memory = qiven3::test::ruled::Memory;
using SQLite = qiven3::test::ruled::Sqlite<>;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::string_view reviewer_lane = "reviewer";

// The widths against what fills them.
static_assert(qiven3::store::field_bytes == qiven3::entry::content::path_size);
static_assert(qiven3::store::id_bytes == std::tuple_size_v<decltype(qiven3::entry::validator::Id::bytes)>);
static_assert(qiven3::store::scope_bytes == std::string_view("project:").size() + 48 &&
              qiven3::store::lane_bytes == 32);

// The error a call raised, as "Kind code", or "none".
template <class F>
std::string raised(const F& f) {
    const std::optional<Refusal> r = refused(f);
    return r ? std::string(qiven3::store::name(r->kind)) + " " + r->code : std::string("none");
}

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
template <class S>
std::vector<GuardView> guard_of(S& store, Vector<LaneHead>& rows) {
    (void)store.heads(std::nullopt, rows);
    std::vector<GuardView> out;
    for (const LaneHead& h : rows) {
        out.push_back(GuardView{h.scope.view(), h.lane.view(), h.head.id.view()});
    }
    return out;
}

// The store F makes, holding A or nothing, its own export imported.
template <class F>
void own_export(qiven3::test::Context& qiven3_ctx, F& factory, const Book& book, bool held) {
    std::vector<std::byte> pages(std::size_t{1} << 20U);
    Arena arena(pages);
    const auto s = std::make_unique<typename F::Store>(factory.make());
    const std::vector<Stored> a = {book.stored("A")};
    const std::vector<EntryView> lines = views(a);
    if (held) {
        (void)s->append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(lines));
    }
    {
        auto source = s->export_(std::nullopt, {});
        Vector<LaneHead> rows{Chars(arena)};
        const std::string said = raised([&] { (void)s->import_(source, rows); });
        CHECK(held ? said.starts_with("StoreNotEmpty") : said == "none");
    }
    const Counts counts = held ? Counts{1, a[0].record ? 1U : 0U, 0, 1} : Counts{};
    CHECK(s->verify() == counts);
    (void)s->close();
    if constexpr (F::Store::capabilities.durable) {
        const auto again = std::make_unique<typename F::Store>(factory.reopen());
        CHECK(again->verify() == counts);
        (void)again->close();
    }
}

}  // namespace

QIVEN3_CASE(checked_reads, widths) {
    for (std::size_t i = 0; i < qiven3::entry::secret::PATTERNS.size(); ++i) {
        CHECK(qiven3::entry::secret::name(static_cast<std::uint8_t>(i)).size() <= qiven3::store::pattern_bytes);
    }
    for (const std::string_view code : qiven3::entry::validator::code_names) {
        CHECK(code.size() <= qiven3::store::code_bytes);
    }
}

QIVEN3_CASE(checked_reads, secret_hit) {
    const auto site = qiven3::test::ruled::site();
    Memory factory(*site);
    Memory::Store store = factory.make();
    const std::vector<Stored> hit = {site->book().with_secret("A", std::nullopt)};
    const std::vector<EntryView> hit_lines = views(hit);
    CHECK_EQ(raised([&] {
                 (void)store.append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(hit_lines));
             }),
             std::string("EntryInvalid secret"));
    CHECK_EQ(store.size().volume.count, 0U);
    CHECK(store.verify() == Counts{});
}

QIVEN3_CASE(checked_reads, whole_record) {
    const auto site = qiven3::test::ruled::site();
    const Book& book = site->book();
    Memory factory(*site);
    Memory::Store store = factory.make();
    std::vector<std::byte> pages(std::size_t{1} << 20U);
    Arena arena(pages);
    const Chars chars(arena);
    const std::vector<Stored> first = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
    const std::vector<EntryView> before = views(first);
    (void)store.append(qiven_scope, implementer_lane, std::nullopt, before);  // what the reviewer's lane names
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
    CHECK_EQ(store.size().volume.count, 7U);
    const std::vector<Stored> whole = {f2, f3};
    const std::vector<EntryView> whole_lines = views(whole);
    const Written w = store.forget(qiven_scope, reviewer_lane, id_of(lane[2]), whole_lines, guard, erased);
    CHECK_EQ(w.head.seq, 5);
    CHECK_EQ(erased.size(), 2U);
    CHECK(store.forgotten(id_of(lane[1])) && store.forgotten(id_of(lane[2])));
    const Counts counts = store.verify();
    CHECK_EQ(counts.entries, 9U);
    CHECK_EQ(counts.forgets, 2U);
}

QIVEN3_CASE(checked_reads, own_export) {
    const auto site = qiven3::test::ruled::site();
    for (const bool held : {false, true}) {
        Memory memory(*site);
        own_export(qiven3_ctx, memory, site->book(), held);
        SQLite sqlite(*site);
        own_export(qiven3_ctx, sqlite, site->book(), held);
    }
}
