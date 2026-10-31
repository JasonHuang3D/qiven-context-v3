// checked/clearing's cases: a false secret hit's clearing on held direction
// records, through the ruled stores and the store's generic calls, on the
// fake, each store from the stores' factory. checked_clearing.memory: a
// content hit refused with no direction record clearing it, or one
// clearing another pattern; admitted on a state an import filled from one
// holding the clearing, then refused there once used; refused once the
// clearing is forgotten, and under a revision for another pattern.
// checked_clearing.memory_many and checked_clearing.sqlite_many: the
// clearing over 4 and over 16 direction records, append's counted peak
// equal, within its budget. checked_clearing.parsed_view: a view's bytes
// parsed, a record's content, a line refused, one shorter than its frame
// refused, none held. checked_clearing.sqlite_short_record: a direction
// record's row cut short, an append's clearing refusing it, the process alive.
// checked_clearing.sqlite_bad_content: a held map's record, its frame whole,
// its content no parser takes, a revision's append refused, the process alive.
// checked_clearing.memory_references and checked_clearing.sqlite_references:
// a map citing A, its 24 references the most its lists hold, every syntax
// of its content among them, appended on each ruled store, its counted
// peak within its budget, 1 MiB plus 4 times the lines given.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/memory.hpp"
#include "support/checked/stores.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
using qiven3::store::GuardView;
using qiven3::store::Id;
using qiven3::store::LaneHead;
using qiven3::memory::Vector;
using qiven3::test::ruled::Memory;
using SQLite = qiven3::test::ruled::Sqlite<>;
using Store = Memory::Store;

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";
constexpr std::string_view reviewer_lane = "reviewer";

// The error a call raised, as "Kind code", or "none".
template <class F>
std::string raised(const F& f) {
    const std::optional<Refusal> r = refused(f);
    return r ? std::string(qiven3::store::name(r->kind)) + " " + r->code : std::string("none");
}

// The corrupt code a call raised, or "none".
template <class F>
std::string corrupt_code(const F& f) {
    try {
        f();
    } catch (const qiven3::store::StoreError& e) {
        return e.corrupt() ? std::string(qiven3::store::name(*e.corrupt())) : std::string("not corrupt");
    }
    return "none";
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
std::vector<GuardView> guard_of(Store& store, Vector<LaneHead>& rows) {
    (void)store.heads(std::nullopt, rows);
    std::vector<GuardView> out;
    for (const LaneHead& h : rows) {
        out.push_back(GuardView{h.scope.view(), h.lane.view(), h.head.id.view()});
    }
    return out;
}

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

// An append's error as raised gives it, on a frame of its own.
std::string appending(SQLite::Store& store, std::string_view lane, const std::vector<EntryView>& lines) {
    return raised([&] { (void)store.append(qiven_scope, lane, std::nullopt, lines); });
}

// n records on reviewer's lane, n - 1 roots clearing sk-key, then a
// revision of the last root clearing aws-akia: every root folded, the last
// with its revision, before the hit passes; append's counted peak.
std::size_t cleared_peak(qiven3::test::Context& qiven3_ctx, qiven3::test::ruled::Site& site, const Book& book,
                         std::size_t n) {
    const std::vector<Stored> hit = {book.made("A", "A", *book.with_secret("A", std::nullopt).content,
                                               [](header::FieldsView& f) { f.at = "2026-10-17T12:00:00.000Z"; })};
    const std::vector<EntryView> hit_lines = views(hit);
    const std::string content = content_of(hit[0]);
    SQLite factory(site);
    const auto owned = std::make_unique<SQLite::Store>(factory.make());  // past a frame's share
    SQLite::Store& store = *owned;
    std::vector<Stored> records;
    records.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::string at = "2026-10-17T09:" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ":00.000Z";
        const Stored* parent = records.empty() ? nullptr : &records.back();
        const bool last = i + 1 == n;
        records.push_back(clearing(book, content, last ? "aws-akia" : "sk-key", parent, last ? parent : nullptr,
                                   static_cast<std::int64_t>(i + 1), at));
    }
    const std::vector<EntryView> lines = views(records);
    CHECK_EQ(appending(store, reviewer_lane, lines), std::string("none"));
    CHECK_EQ(appending(store, implementer_lane, hit_lines), std::string("none"));
    CHECK(store.peak() <= *qiven3::store::operation_budget(*qiven3::store::given(hit_lines)));
    return store.peak();
}

}  // namespace

// A false secret hit's clearing on held direction records, on the fake.
// A store's view, its bytes as given.
struct Bytes {
    std::string line;
    std::string record;
    bool header(std::string_view /*id*/, qiven3::memory::String& out) const { return !out.assign(line).empty(); }
    bool body(std::string_view /*id*/, qiven3::memory::String& out) const { return !out.assign(record).empty(); }
};

QIVEN3_CASE(checked_clearing, parsed_view) {
    const auto site = qiven3::test::ruled::site();
    const Stored a = site->book().stored("A");
    Bytes bytes{a.line, *a.record};
    qiven3::checked::reads::ParsedView<Bytes> view(bytes, {qiven3::store::Operation::append, "here"});
    std::vector<std::byte> pages(1U << 20U);
    qiven3::memory::Arena arena(pages);
    qiven3::checked::Header h{Chars(arena)};
    qiven3::memory::String content{Chars(arena)};
    const auto refused_as = [&](std::string_view id) { return corrupt_code([&] { view.header(id, h); }); };
    CHECK(view.header(id_of(a), h) && h.value(Slot::id) == id_of(a));
    CHECK(view.body(id_of(a), content) && std::string_view(content) == *a.content);
    CHECK_EQ(refused_as("sha256:" + std::string(64, '0')), std::string("id_mismatch"));
    bytes.line = "{a line no parser takes";
    CHECK_EQ(refused_as(id_of(a)), std::string("incoherent"));
    for (std::size_t n = 1; n < qiven3::entry::body::frame_size; ++n) {
        bytes.record = a.record->substr(0, n);
        CHECK_EQ(corrupt_code([&] { (void)view.body(id_of(a), content); }), std::string("incoherent"));
    }
    bytes = Bytes{};
    CHECK(!view.header(id_of(a), h) && !view.body(id_of(a), content));
}

// The one direction record clearing the hit, its row cut shorter each time,
// to no bytes: the append's refusal is the record's, the secret's never
// reached; the ruled read and verify the same, after a reopen too.
QIVEN3_CASE(checked_clearing, sqlite_short_record) {
    const auto site = qiven3::test::ruled::site();
    const Book& book = site->book();
    const std::vector<Stored> hit = {book.made("A", "A", *book.with_secret("A", std::nullopt).content,
                                               [](header::FieldsView& f) { f.at = "2026-10-17T12:00:00.000Z"; })};
    const std::vector<EntryView> hit_lines = views(hit);
    const std::vector<Stored> direction = {
        clearing(book, content_of(hit[0]), "aws-akia", nullptr, nullptr, 1, "2026-10-17T09:00:00.000Z")};
    SQLite factory(*site);
    const auto store = std::make_unique<SQLite::Store>(factory.make());  // past a frame's share
    CHECK_EQ(appending(*store, reviewer_lane, views(direction)), std::string("none"));
    std::vector<std::byte> pages(1U << 16U);
    qiven3::memory::Arena arena(pages);
    qiven3::memory::String record{Chars(arena)};
    const std::string id = id_of(direction[0]);
    const std::string denied = "incoherent";
    constexpr qiven3::store::WhereView at = qiven3::store::sqlite::site("test", "scratch");
    for (const int n : {163, 100, 10, 0}) {
        store->connection().exec(
            "UPDATE body SET record = substr(record, 1, " + std::to_string(n) + ") WHERE id = '" + id + "'", at);
        CHECK_EQ(corrupt_code([&] { (void)store->append(qiven_scope, implementer_lane, std::nullopt, hit_lines); }),
                 denied);
        CHECK_EQ(corrupt_code([&] { (void)store->body(id, record); }), denied);
        CHECK_EQ(corrupt_code([&] { (void)store->verify(); }), denied);
    }
    (void)store->close();
    const auto again = std::make_unique<SQLite::Store>(factory.reopen());
    CHECK_EQ(corrupt_code([&] { (void)again->verify(); }), denied);
    (void)again->close();
}

// The writes verified, reopened and moved by the own export; then M1's content
// one byte off, its frame whole: M2's append refused incoherent, the process alive.
QIVEN3_CASE(checked_clearing, sqlite_bad_content) {
    const auto site = qiven3::test::ruled::site();
    const Book& book = site->book();
    SQLite factory(*site);
    SQLite fresh(*site);
    auto store = std::make_unique<SQLite::Store>(factory.make());  // past a frame's share
    const std::vector<Stored> a = {book.stored("A")};
    const std::vector<Stored> theirs = {book.stored("C"), book.stored("M1")};
    const std::vector<Stored> m2 = {book.stored("M2")};
    CHECK_EQ(appending(*store, implementer_lane, views(a)), std::string("none"));
    CHECK_EQ(appending(*store, reviewer_lane, views(theirs)), std::string("none"));
    const qiven3::store::Counts held = store->verify();
    (void)store->close();
    store = std::make_unique<SQLite::Store>(factory.reopen());
    const auto copy = std::make_unique<SQLite::Store>(fresh.make());
    std::vector<std::byte> pages(1U << 16U);
    qiven3::memory::Arena arena(pages);
    Vector<LaneHead> rows{Chars(arena)};
    auto source = store->export_(std::nullopt, {});
    (void)copy->import_(source, rows);
    CHECK(store->verify() == held && copy->verify() == held);
    (void)copy->close();
    CHECK(std::make_unique<SQLite::Store>(fresh.reopen())->verify() == held);
    constexpr qiven3::store::WhereView at = qiven3::store::sqlite::site("test", "scratch");
    const std::string m1 = id_of(theirs[1]);
    store->connection().exec(
        "UPDATE body SET record = CAST(substr(record, 1, 11) || 'x' || substr(record, 13) AS BLOB) WHERE id = '" + m1 +
            "'",
        at);
    CHECK_EQ(corrupt_code([&] { (void)store->append(qiven_scope, reviewer_lane, m1, views(m2)); }),
             std::string("incoherent"));
    (void)store->close();
}

QIVEN3_CASE(checked_clearing, memory) {
    const auto site = qiven3::test::ruled::site();
    const Book& book = site->book();
    Memory mine(*site);
    Memory theirs(*site);
    Store store = mine.make();
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
    Store into = theirs.make();
    {
        auto x = store.export_(std::nullopt, {});
        Vector<LaneHead> wrote(site->chars());
        (void)into.import_(x, wrote);
        x.close();
    }

    // The clearing forgotten: the hit refused again.
    const std::vector<Stored> gone = {
        forget_of(book.stored("E"), clear[0], clear[0], reviewer_lane, 3, "2026-10-17T11:00:00.000Z")};
    const std::vector<EntryView> gone_lines = views(gone);
    Vector<LaneHead> rows(site->chars());
    const std::vector<GuardView> guard = guard_of(store, rows);
    Vector<Id> erased(site->chars());
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
QIVEN3_CASE(checked_clearing, memory_many) {
    const auto site = qiven3::test::ruled::site();
    const Book& book = site->book();
    const std::vector<Stored> hit = {book.made("A", "A", *book.with_secret("A", std::nullopt).content,
                                               [](header::FieldsView& f) { f.at = "2026-10-17T12:00:00.000Z"; })};
    const std::vector<EntryView> hit_lines = views(hit);
    const std::string content = content_of(hit[0]);
    // n records on reviewer's lane, n - 1 roots clearing sk-key, then a
    // revision of the last root clearing aws-akia: every root folded, the
    // last with its revision, before the hit passes.
    const auto peak_with = [&](std::size_t n) {
        Memory factory(*site);
        Store store = factory.make();
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
    const std::size_t four = peak_with(4);
    const std::size_t sixteen = peak_with(16);
    std::cout << "checked_clearing.memory_many: append " << four << " bytes counted with 4 records, " << sixteen
              << " with 16, of " << qiven3::store::state::budget_of(hit_lines) << '\n';
    CHECK_EQ(four, sixteen);
    CHECK(sixteen <= qiven3::store::state::budget_of(hit_lines));
}

QIVEN3_CASE(checked_clearing, sqlite_many) {
    const auto site = qiven3::test::ruled::site();
    const Book& book = site->book();
    const std::size_t four = cleared_peak(qiven3_ctx, *site, book, 4);
    const std::size_t sixteen = cleared_peak(qiven3_ctx, *site, book, 16);
    std::cout << "checked_clearing.sqlite_many: append " << four << " bytes counted with 4 records, " << sixteen
              << " with 16\n";
    CHECK_EQ(four, sixteen);
}

namespace {

namespace stage_book = qiven3::test::stage_book;

template <class F>
void references_case(qiven3::test::Context& qiven3_ctx, F& f, std::string_view name) {
    const stage_book::Book& book = f.site().book();
    const std::vector<stage_book::Stored> a = {book.stored("A")};
    const std::vector<stage_book::Stored> map = {book.with_references(24, stage_book::id_of(a[0]))};
    const std::vector<qiven3::store::EntryView> lines = stage_book::views(map);
    const auto store = std::make_unique<typename F::Store>(f.make());
    (void)store->append(qiven_scope, implementer_lane, std::nullopt, stage_book::views(a));
    std::string refused = "none";
    try {
        if (const std::optional<stage_book::Refusal> r =
                stage_book::refused([&] { (void)store->append(qiven_scope, "reviewer", std::nullopt, lines); })) {
            refused = r->code + " " + r->field + " " + r->detail;
        }
    } catch (const qiven3::memory::MemoryError&) {
        refused = "MemoryError";
    }
    const std::size_t budget = *qiven3::store::operation_budget(*qiven3::store::given(lines));
    std::cout << name << ": content " << map[0].content->size() << " bytes, refused " << refused << ", peak "
              << store->peak() << " of " << budget << "\n";
    CHECK_EQ(refused, std::string("none"));
    CHECK(store->peak() <= budget);
    CHECK_EQ(store->head(qiven_scope, "reviewer").value_or(qiven3::store::Head{}).seq, 1);
    (void)store->close();
}

}  // namespace

QIVEN3_CASE(checked_clearing, memory_references) {
    const auto site = qiven3::test::ruled::site();
    Memory f(*site);
    references_case(qiven3_ctx, f, "checked_clearing.memory_references");
}

QIVEN3_CASE(checked_clearing, sqlite_references) {
    const auto site = qiven3::test::ruled::site();
    SQLite f(*site);
    references_case(qiven3_ctx, f, "checked_clearing.sqlite_references");
}
