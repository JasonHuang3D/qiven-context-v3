// checked's rules through its ruled stores, the store's cases that test a
// rule of the entry's, moved from them, on the fake. widths: the store's
// fields against the entry's (an id, a field's path, a scope and a lane at
// their syntaxes' longest; every pattern's name and every validator code
// whole). secret_hit: a content hit refused secret at append, nothing
// written. whole_record: a forget of M1 alone refused whole_record, M2
// revising it; then M2 and M1 forgotten together. own_export: a ruled
// store's own export imported, into itself empty, refused StoreNotEmpty
// once it holds an entry. These three in memory and in SQLite, each write
// followed by verify and, on SQLite, a reopen and verify again. corrupt: a
// held line or body record changed under the lock is StoreCorrupt at
// header, body and verify, and served again once restored, a body record's
// digit body_mismatch; a held line given another held entry's, header's
// id_mismatch. sqlite_corrupt: the same through SQLite's connection, and a
// parent column beside its line the ruled scan's incoherent. sqlite_columns:
// each of A's scope, seq and kind columns altered beside its line, the
// ruled scan's incoherent, then put back. forged: the ruled verify over a
// state forged under the lock. head_mismatch: a SQLite source's lines held,
// imported with heads short of theirs, refused, nothing left.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "checked/stores.hpp"
#include "entry/content_fields.hpp"
#include "entry/header.hpp"
#include "entry/secret.hpp"
#include "entry/validator.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/memory.hpp"
#include "support/checked/stores.hpp"
#include "support/fake_platform/fake_platform.hpp"
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
void own_export(qiven::runner::Context& qiven_ctx, F& factory, const Book& book, bool held) {
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

QIVEN_CASE(checked_reads, widths) {
    for (std::size_t i = 0; i < qiven3::entry::secret::PATTERNS.size(); ++i) {
        CHECK(qiven3::entry::secret::name(static_cast<std::uint8_t>(i)).size() <= qiven3::store::pattern_bytes);
    }
    for (const std::string_view code : qiven3::entry::validator::code_names) {
        CHECK(code.size() <= qiven3::store::code_bytes);
    }
}

namespace {

// The store F makes, a content hit given: nothing written.
template <class F>
void secret_hit(qiven::runner::Context& qiven_ctx, F& factory, const Book& book) {
    const auto owned = std::make_unique<typename F::Store>(factory.make());  // past a frame's share
    typename F::Store& store = *owned;
    const std::vector<Stored> hit = {book.with_secret("A", std::nullopt)};
    const std::vector<EntryView> hit_lines = views(hit);
    CHECK_EQ(raised([&] {
                 (void)store.append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(hit_lines));
             }),
             std::string("EntryInvalid secret"));
    CHECK_EQ(store.size().volume.count, 0U);
    CHECK(store.verify() == Counts{});
}

// The store F makes, the reviewer's M1 forgotten alone and with M2.
template <class F>
void whole_record(qiven::runner::Context& qiven_ctx, F& factory, const Book& book) {
    const auto owned = std::make_unique<typename F::Store>(factory.make());  // past a frame's share
    typename F::Store& store = *owned;
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
    CHECK(w.purge.done);
    CHECK(store.forgotten(id_of(lane[1])) && store.forgotten(id_of(lane[2])));
    const Counts counts = store.verify();
    CHECK_EQ(counts.entries, 9U);
    CHECK_EQ(counts.forgets, 2U);
    (void)store.close();
    if constexpr (F::Store::capabilities.durable) {
        const auto again = std::make_unique<typename F::Store>(factory.reopen());
        CHECK(again->verify() == counts);
        (void)again->close();
    }
}

}  // namespace

// Each on memory and on SQLite.
QIVEN_CASE(checked_reads, secret_hit) {
    const auto site = qiven3::test::ruled::site();
    Memory memory(*site);
    secret_hit(qiven_ctx, memory, site->book());
    SQLite sqlite(*site);
    secret_hit(qiven_ctx, sqlite, site->book());
}

QIVEN_CASE(checked_reads, whole_record) {
    const auto site = qiven3::test::ruled::site();
    Memory memory(*site);
    whole_record(qiven_ctx, memory, site->book());
    SQLite sqlite(*site);
    whole_record(qiven_ctx, sqlite, site->book());
}

QIVEN_CASE(checked_reads, own_export) {
    const auto site = qiven3::test::ruled::site();
    for (const bool held : {false, true}) {
        Memory memory(*site);
        own_export(qiven_ctx, memory, site->book(), held);
        SQLite sqlite(*site);
        own_export(qiven_ctx, sqlite, site->book(), held);
    }
}

namespace {

namespace state = qiven3::store::state;
using qiven3::checked::Header;
using qiven3::memory::String;
using qiven3::store::Head;
using Store = qiven3::checked::MemoryStore<Fake>;

// The fake, the fixtures, a state holding A, B, D and ACC, E's lines (a
// forget of B), and an arena.
struct Bed {
    Bed()
        : fake(qiven3::platform::fake::Config{}), book(fake), heap(fake.pages(std::size_t{8} << 20U)),
          state(heap.bytes(), Limits{}), lane{book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")},
          e{book.stored("E")}, e_lines(views(e)), pages(std::size_t{1} << 20U), arena(pages), chars(arena) {
        Store store(state, fake);
        const std::vector<EntryView> lines = views(lane);
        (void)store.append(qiven_scope, implementer_lane, std::nullopt, lines);
        acc = id_of(lane[3]);
        guard.push_back(GuardView{qiven_scope, implementer_lane, acc});
    }
    Fake fake;
    Book book;
    qiven3::platform::fake::FakePages heap;
    state::State state;
    std::vector<Stored> lane;
    std::vector<Stored> e;
    std::vector<EntryView> e_lines;
    std::string acc;  // ACC's id, the guard's view's
    std::vector<GuardView> guard;
    std::vector<std::byte> pages;
    Arena arena;
    Chars chars;
};

// The StoreCorrupt code a call raised, "none", or "not corrupt".
template <class F>
std::string corrupt_code(const F& f) {
    try {
        f();
    } catch (const qiven3::store::StoreError& e) {
        return e.corrupt() ? std::string(qiven3::store::name(*e.corrupt())) : std::string("not corrupt");
    }
    return "none";
}

}  // namespace

QIVEN_CASE(checked_reads, corrupt) {
    const auto b = std::make_unique<Bed>();
    Store store(b->state, b->fake);
    const std::string a = id_of(b->lane[0]);
    Header h(b->chars);
    String record(b->chars);
    const auto corrupt = [](const auto& f) {
        const std::optional<Refusal> r = refused(f);
        return r.has_value() && r->kind == Kind::corrupt;
    };
    // One character of A's line or body record set, the one it was given.
    const auto set = [&](bool body, std::size_t at, char to) {
        const state::Locked lock(b->state, Operation::size, {});
        state::Held& e = lock.data().held[0];
        auto& text = body ? e.body : e.line;
        const char from = text[at];
        text[at] = to;
        return from;
    };
    // A's writer changed: its id no longer its own.
    const std::size_t writer = b->lane[0].line.find("implementer/") + 1;
    const char m = set(false, writer, 'n');
    CHECK(corrupt([&] { store.header(a, h); }));
    CHECK(corrupt([&] { (void)store.body(a, record); }));
    CHECK(corrupt([&] { (void)store.verify(); }));
    (void)set(false, writer, m);
    store.header(a, h);
    // A digit of A's body record changed.
    const std::size_t digit = b->lane[0].record->find_last_of("0123456789");
    const char was = set(true, digit, b->lane[0].record->at(digit) == '0' ? '1' : '0');
    CHECK_EQ(corrupt_code([&] { (void)store.body(a, record); }), std::string("body_mismatch"));
    CHECK(corrupt([&] { (void)store.verify(); }));
    (void)set(true, digit, was);
    CHECK(store.body(a, record));
    CHECK_EQ(store.verify().entries, 4U);
    // A's line B's: a header line whole, of another id.
    const std::string line = b->lane[0].line;
    {
        const state::Locked lock(b->state, Operation::size, {});
        lock.data().held[0].line.assign(b->lane[1].line);
    }
    CHECK_EQ(corrupt_code([&] { store.header(a, h); }), std::string("id_mismatch"));
    {
        const state::Locked lock(b->state, Operation::size, {});
        lock.data().held[0].line.assign(line);
    }
    store.header(a, h);
}

// The same in SQLite, a row changed through the store's connection: A's
// body record B's, body_mismatch; B's parent column cleared beside its
// line, the ruled scan's incoherent; A's line B's, id_mismatch at the
// header read and the scan.
QIVEN_CASE(checked_reads, sqlite_corrupt) {
    const auto site = qiven3::test::ruled::site();
    SQLite factory(*site);
    const auto store = std::make_unique<SQLite::Store>(factory.make());  // past a frame's share
    std::vector<std::byte> pages(std::size_t{1} << 17U);
    Arena arena(pages);
    const Chars chars(arena);
    const std::vector<Stored> mine = {site->book().stored("A"), site->book().stored("B")};
    const std::vector<EntryView> lines = views(mine);
    (void)store->append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(lines));
    const std::string a = id_of(mine[0]);
    const std::string b = id_of(mine[1]);
    const auto h = std::make_unique<Header>(chars);  // past a frame's share
    String record(chars);
    const auto scanned = [&] {
        for (auto scan = store->scan({}); scan.next(*h);) {
        }
    };
    constexpr qiven3::store::WhereView at = qiven3::store::sqlite::site("test", "scratch");
    qiven3::store::sqlite::Connection& c = store->connection();
    c.exec("UPDATE body SET record = (SELECT record FROM body WHERE id = '" + b + "') WHERE id = '" + a + "'", at);
    CHECK_EQ(corrupt_code([&] { (void)store->body(a, record); }), std::string("body_mismatch"));
    c.exec("UPDATE entry SET parent = NULL WHERE id = '" + b + "'", at);
    CHECK_EQ(corrupt_code(scanned), std::string("incoherent"));
    c.exec("UPDATE entry SET line = (SELECT line FROM entry WHERE id = '" + b + "') WHERE id = '" + a + "'", at);
    CHECK_EQ(corrupt_code([&] { store->header(a, *h); }), std::string("id_mismatch"));
    CHECK_EQ(corrupt_code(scanned), std::string("id_mismatch"));
}

// Each of A's columns altered beside its line, in SQLite: the ruled scan
// StoreCorrupt incoherent; then put back, A's header read again.
QIVEN_CASE(checked_reads, sqlite_columns) {
    const auto site = qiven3::test::ruled::site();
    SQLite factory(*site);
    const auto store = std::make_unique<SQLite::Store>(factory.make());  // past a frame's share
    const std::vector<Stored> mine = {site->book().stored("A"), site->book().stored("B")};
    const std::vector<EntryView> lines = views(mine);
    (void)store->append(qiven_scope, implementer_lane, std::nullopt, std::span<const EntryView>(lines));
    const std::string a = id_of(mine[0]);
    const std::array<std::string, 3> alters = {"scope = scope || 'x'", "seq = seq + 100", "kind = kind || 'x'"};
    const std::array<std::string, 3> backs = {"scope = substr(scope, 1, length(scope) - 1)", "seq = seq - 100",
                                              "kind = substr(kind, 1, length(kind) - 1)"};
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    Arena arena(pages);
    constexpr qiven3::store::WhereView at = qiven3::store::sqlite::site("test", "scratch");
    for (std::size_t i = 0; i < alters.size(); ++i) {
        store->connection().exec("UPDATE entry SET " + alters.at(i) + " WHERE id = '" + a + "'", at);
        Header h{Chars(arena)};
        const std::optional<Refusal> r = refused([&] {
            for (auto scan = store->scan({}); scan.next(h);) {
            }
        });
        CHECK(r.has_value() && r->kind == Kind::corrupt && r->detail == "an entry's column differs from its line");
        store->connection().exec("UPDATE entry SET " + backs.at(i) + " WHERE id = '" + a + "'", at);
        store->header(a, h);
    }
}

namespace {

// A fresh bed's state, E's forget of B first where asked, forged under its
// lock by change; then the ruled store's verify's refusal code, or "none".
template <class F>
std::string forged(bool forget, const F& change) {
    const auto b = std::make_unique<Bed>();
    Store store(b->state, b->fake);
    if (forget) {
        Vector<Id> erased(b->chars);
        (void)store.forget(qiven_scope, implementer_lane, b->acc, b->e_lines, b->guard, erased);
    }
    {
        const state::Locked lock(b->state, Operation::size, {});
        change(*b, lock.data());
    }
    Store verifier(b->state, b->fake);
    return corrupt_code([&] { (void)verifier.verify(); });
}

// Entry i made s: its line, body record and id; the ids sorted again.
void replace(state::Data& d, std::uint32_t i, const Stored& s) {
    state::Held& e = d.held[i];
    e.line.assign(s.line);
    e.body.assign(*s.record);
    e.id = Id::of(id_of(s));
    state::sort_ids(d);
}

}  // namespace

// The ruled store's verify, which reads each line, over a state forged
// under the lock: a line whose parent, or seq, is not its place's, and a
// target index not its header's, incoherent; a forgotten entry's body kept,
// forgotten_body.
QIVEN_CASE(checked_reads, forged) {
    CHECK_EQ(forged(false, [](Bed&, state::Data&) {}), std::string("none"));
    CHECK_EQ(forged(false,
                    [](Bed& b, state::Data& d) {
                        const std::string parent = id_of(b.lane[2]);
                        replace(d, 1, b.book.made("B", "B", *b.lane[1].content,
                                                  [&](header::FieldsView& f) { f.parent = parent; }));
                    }),
             std::string("incoherent"));
    CHECK_EQ(forged(false,
                    [](Bed& b, state::Data& d) {
                        replace(d, 3,
                                b.book.made("ACC", "ACC", *b.lane[3].content,
                                            [](header::FieldsView& f) { f.seq = 5; }));
                        d.held[3].seq = 5;
                        d.lanes[0].head = Head{d.held[3].id, 5};
                    }),
             std::string("incoherent"));
    CHECK_EQ(forged(false, [](Bed&, state::Data& d) { d.held[3].target = 0; }), std::string("incoherent"));
    CHECK_EQ(forged(true,
                    [](Bed& b, state::Data& d) {
                        d.held[1].body.assign(*b.lane[1].record);
                        d.held[1].has_body = true;
                        d.size += b.lane[1].record->size() + 1U;
                    }),
             std::string("forgotten_body"));
}

namespace {

using qiven3::store::Line;

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
        return line.keys.assign(keyed(lines[at - 1].first, record).keys);
    }
};

// An export's lines into held, and their count.
template <class E>
std::size_t drain(E& e, Held& held) {
    std::vector<std::byte> pages(std::size_t{1} << 18U);
    std::size_t n = 0;
    for (;;) {
        Arena arena(pages);
        Line line(arena);
        if (!e.next(line)) {
            return n;
        }
        ++n;
        held.lines.emplace_back(std::string(line.header),
                                line.has_body ? std::optional<std::string>(std::string(line.body)) : std::nullopt);
    }
}

}  // namespace

// The ruled SQLite store's import of a source whose heads differ from its
// lines' (the store's exchange smoke case's, moved): A, B, D and ACC on
// one lane, C and M1 on another, exported and held; the heads given short
// of the lines': head_mismatch, nothing left.
QIVEN_CASE(checked_reads, head_mismatch) {
    const auto site = qiven3::test::ruled::site();
    const Book& book = site->book();
    SQLite from(*site);
    const auto source = std::make_unique<SQLite::Store>(from.make());
    const std::vector<Stored> mine = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
    const std::vector<Stored> theirs = {book.stored("C"), book.stored("M1")};
    (void)source->append(qiven_scope, implementer_lane, std::nullopt, views(mine));
    (void)source->append(qiven_scope, reviewer_lane, std::nullopt, views(theirs));
    std::vector<std::byte> pages(std::size_t{1} << 20U);
    Arena arena(pages);
    Held copy;
    {
        auto all = source->export_(std::nullopt, {});
        Vector<LaneHead> rows{Chars(arena)};
        (void)all.heads(rows);
        copy.given.assign(rows.begin(), rows.end());
        CHECK_EQ(drain(all, copy), 6U);
    }
    copy.given.pop_back();
    SQLite to(*site);
    const auto refused = std::make_unique<SQLite::Store>(to.make());
    Vector<LaneHead> heads{Chars(arena)};
    CHECK_EQ(raised([&] { (void)refused->import_(copy, heads); }), "EntryInvalid head_mismatch");
    CHECK(heads.empty());
    CHECK(refused->verify() == Counts{});
}
