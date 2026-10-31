// checked/rules' cases over the store's stage, on the fixtures over a view
// in memory: check before any lock and admit inside it. An append of implementer's lane staged in
// two calls; a forget of E on it, never refused for size; forget_kind
// first, entry by entry, a line the strict parser refuses EntryInvalid
// with its code, then part 1 (id_mismatch, no id); a forget appended
// forget_path; a content hit (aws-akia) refused secret with no id, admitted
// once cleared for its content and pattern, refused when the clearing is
// another pattern's, when a held header has the content, or an earlier
// entry of the call; a header hit refused; part 2's first fault with its
// code; the maximum by count and by size, at it admitted; at import part 1
// alone, the chain rules as each line arrives (a line again, a gap), the
// final view's codes (forgotten_body, body_missing, validator's target,
// identity and forgotten_target, part 2's before forgotten_body, a whole
// record standing); head_mismatch apart; a target of another scope or itself
// at append and import. The ruled stores' append takes their rules alone,
// evaluated by the compiler.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "checked/rules.hpp"
#include "checked/stores.hpp"
#include "entry/body.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "qiven/runner/runner.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/stage.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/view.hpp"
#include "support/fixtures/stage_book.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)

constexpr std::string_view qiven_scope = "project:qiven";
constexpr std::string_view implementer_lane = "implementer";

// Whether store S's append takes hook A: a ruled store its rules alone, a
// plain one any.
template <class S, class A>
concept Appends = requires(S& s, std::span<const EntryView> lines, A a) {
    s.append(qiven_scope, implementer_lane, std::nullopt, lines, a);
};
static_assert(Appends<qiven3::store::MemoryStore<Fake>, qiven3::store::NoAdmit>);
static_assert(Appends<qiven3::checked::MemoryStore<Fake>, checked::Rules<Fake>>);
static_assert(!Appends<qiven3::checked::MemoryStore<Fake>, qiven3::store::NoAdmit>);
static_assert(Appends<qiven3::checked::SQLiteStore<Fake>, checked::Rules<Fake>>);
static_assert(!Appends<qiven3::checked::SQLiteStore<Fake>, qiven3::store::NoAdmit>);

}  // namespace

QIVEN_CASE(store_stage, append) {
    const auto bench = std::make_unique<Bench>(4U << 20U);
    Fake& fake = bench->fake;
    const Book& book = bench->book;
    const Chars chars = bench->chars;
    ScratchPages& scratch = bench->scratch;
    fixtures::MemoryView view;
    const std::vector<Stored> first = {book.stored("A"), book.stored("B")};
    const std::vector<EntryView> lines = views(first);
    const checked::Checked<Fake> checked = checked::check(fake, std::span<const EntryView>(lines), at(Operation::append), chars, scratch);
    CHECK_EQ(checked.entries.size(), 2U);
    CHECK(checked.hits.empty());
    CHECK_EQ(checked.size, added(first));
    const stage::Staged staged =
        checked::admit(view, qiven_scope, implementer_lane, std::nullopt, checked, Limits{}, at(Operation::append), chars);
    CHECK_EQ(staged.head.id.view(), id_of(first[1]));
    CHECK_EQ(staged.head.seq, 2);
    CHECK_EQ(staged.count, 2U);
    CHECK_EQ(staged.size, added(first));
    hold(view, first);
    view.volume(staged.size, staged.count);
    const std::vector<Stored> second = {book.stored("D")};
    const std::vector<EntryView> more = views(second);
    const checked::Checked<Fake> again = checked::check(fake, std::span<const EntryView>(more), at(Operation::append), chars, scratch);
    const stage::Staged next =
        checked::admit(view, qiven_scope, implementer_lane, id_of(first[1]), again, Limits{}, at(Operation::append), chars);
    CHECK_EQ(next.head.id.view(), id_of(second[0]));
    CHECK_EQ(next.head.seq, 3);
}

QIVEN_CASE(store_stage, forget) {
    const auto bench = std::make_unique<Bench>(4U << 20U);
    Fake& fake = bench->fake;
    const Book& book = bench->book;
    const Chars chars = bench->chars;
    ScratchPages& scratch = bench->scratch;
    fixtures::MemoryView view;
    hold(view, {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")});
    view.volume(1U << 20U, 4);
    const Stored e = book.stored("E");
    const Stored a = book.stored("A");
    // A forget is never refused for size: past the maximum by count.
    Limits small;
    small.target_count = 1;
    small.warning_count = 1;
    small.max_count = 1;
    const std::vector<Stored> call = {e};
    const std::vector<EntryView> lines = views(call);
    const checked::Checked<Fake> checked = checked::check(fake, std::span<const EntryView>(lines), at(Operation::forget), chars, scratch);
    CHECK_EQ(checked.size, added({e}));
    const stage::Staged staged = checked::admit(view, qiven_scope, implementer_lane, id_of(book.stored("ACC")), checked, small,
                                              at(Operation::forget), chars);
    CHECK_EQ(staged.head.id.view(), id_of(e));
    CHECK_EQ(staged.head.seq, 5);
    // forget_kind first, entry by entry, naming the entry.
    const std::optional<Refusal> kind0 = refused_check(fake, {a}, Operation::forget, chars);
    REQUIRE(kind0.has_value());
    CHECK(kind0->kind == Kind::store_error);
    CHECK_EQ(kind0->code, std::string("forget_kind"));
    CHECK_EQ(kind0->detail, std::string("entry 0"));
    CHECK_EQ(kind0->operation, std::string("forget"));
    const std::optional<Refusal> kind1 = refused_check(fake, {e, a}, Operation::forget, chars);
    REQUIRE(kind1.has_value());
    CHECK_EQ(kind1->detail, std::string("entry 1"));
    const Stored broken{"{\"kind\":\"forget\"", std::nullopt, std::nullopt};
    const std::optional<Refusal> parse = refused_check(fake, {e, broken}, Operation::forget, chars);
    REQUIRE(parse.has_value());
    CHECK(parse->kind == Kind::entry_invalid);
    CHECK_EQ(parse->index, 1U);
    CHECK(parse->id.empty());
    std::cout << "store_stage.forget: the parser's code " << parse->code << "\n";
    CHECK(!parse->code.empty());
    const std::optional<Refusal> before = refused_check(fake, {a, broken}, Operation::forget, chars);
    REQUIRE(before.has_value());
    CHECK_EQ(before->code, std::string("forget_kind"));
    // Then part 1: a forget whose at moved is id_mismatch, with no id.
    Stored moved = e;
    const std::size_t t = moved.line.find("\"at\":\"") + 6;
    moved.line[t + 3] = moved.line[t + 3] == '9' ? '8' : '9';
    const std::optional<Refusal> mismatch = refused_check(fake, {moved}, Operation::forget, chars);
    REQUIRE(mismatch.has_value());
    CHECK_EQ(mismatch->code, std::string("id_mismatch"));
    CHECK(mismatch->id.empty());
    // An append takes no forget.
    const std::optional<Refusal> path = refused_check(fake, {e}, Operation::append, chars);
    REQUIRE(path.has_value());
    CHECK_EQ(path->code, std::string("forget_path"));
    CHECK_EQ(path->id, std::string(id_of(e)));
    CHECK_EQ(path->field, std::string("header.kind"));
}

QIVEN_CASE(store_stage, secret) {
    const auto bench = std::make_unique<Bench>(8U << 20U);
    Fake& fake = bench->fake;
    const Book& book = bench->book;
    const Chars chars = bench->chars;
    ScratchPages& scratch = bench->scratch;
    const Stored x = book.with_secret("A", std::nullopt);
    const std::vector<Stored> call = {x};
    const std::vector<EntryView> lines = views(call);
    const checked::Checked<Fake> checked = checked::check(fake, std::span<const EntryView>(lines), at(Operation::append), chars, scratch);
    REQUIRE(checked.hits.size() == 1U);
    CHECK_EQ(qiven3::entry::secret::name(checked.hits[0].hit.pattern), std::string_view("aws-akia"));
    CHECK_EQ(checked.hits[0].hit.path.view(), std::string_view("admit.mistake"));
    // Not cleared: secret, the pattern and the path, no id.
    {
        fixtures::MemoryView view;
        const std::optional<Refusal> r = refused_admit(view, checked, std::nullopt, chars);
        REQUIRE(r.has_value());
        CHECK_EQ(r->code, std::string("secret"));
        CHECK_EQ(r->pattern, std::string("aws-akia"));
        CHECK_EQ(r->field, std::string("admit.mistake"));
        CHECK(r->id.empty());
    }
    // Cleared for its content and pattern: admitted; for another pattern not.
    {
        fixtures::MemoryView view;
        hold(view, {book.clearing(x, "aws-akia")});
        CHECK(!refused_admit(view, checked, std::nullopt, chars).has_value());
        fixtures::MemoryView other;
        hold(other, {book.clearing(x, "sk-key")});
        CHECK(refused_admit(other, checked, std::nullopt, chars).has_value());
    }
    // Used: a held header with the content (on reviewer's lane, so the
    // lane's head stays none), or an earlier entry of the call.
    {
        fixtures::MemoryView view;
        hold(view, {book.clearing(x, "aws-akia"), book.with_secret("C", std::nullopt)});
        const std::optional<Refusal> used = refused_admit(view, checked, std::nullopt, chars);
        REQUIRE(used.has_value());
        CHECK_EQ(used->code, std::string("secret"));
        const Stored y = book.with_secret("B", id_of(x));
        const std::vector<Stored> pair = {x, y};
        const std::vector<EntryView> two = views(pair);
        const checked::Checked<Fake> both =
            checked::check(fake, std::span<const EntryView>(two), at(Operation::append), chars, scratch);
        CHECK_EQ(both.hits.size(), 2U);
        fixtures::MemoryView fresh;
        hold(fresh, {book.clearing(x, "aws-akia")});
        const std::optional<Refusal> r = refused_admit(fresh, both, std::nullopt, chars);
        REQUIRE(r.has_value());
        CHECK_EQ(r->code, std::string("secret"));
        CHECK_EQ(r->index, 1U);
        // The hits settled before part 2: y alone, its parent not held.
        const std::vector<Stored> alone = {y};
        const std::vector<EntryView> one = views(alone);
        const checked::Checked<Fake> second =
            checked::check(fake, std::span<const EntryView>(one), at(Operation::append), chars, scratch);
        fixtures::MemoryView none;
        const std::optional<Refusal> first = refused_admit(none, second, std::nullopt, chars);
        REQUIRE(first.has_value());
        CHECK_EQ(first->code, std::string("secret"));
    }
    // A header hit is refused at check, never cleared.
    const std::optional<Refusal> lane = refused_check(fake, {book.on_lane("A", slack_lane())}, Operation::append, chars);
    REQUIRE(lane.has_value());
    CHECK_EQ(lane->code, std::string("secret"));
    CHECK_EQ(lane->field, std::string("header.lane"));
    CHECK_EQ(lane->pattern, std::string("slack-xox"));
    CHECK(lane->id.empty());
    // Import runs no secret check.
    const checked::Checked<Fake> imported =
        checked::check(fake, std::span<const EntryView>(lines), at(Operation::import_), chars, scratch);
    CHECK(imported.hits.empty());
}

QIVEN_CASE(store_stage, part2) {
    const auto bench = std::make_unique<Bench>(4U << 20U);
    Fake& fake = bench->fake;
    const Book& book = bench->book;
    const Chars chars = bench->chars;
    ScratchPages& scratch = bench->scratch;
    const Stored b = book.stored("B");
    const std::vector<Stored> call = {b};
    const std::vector<EntryView> lines = views(call);
    const checked::Checked<Fake> checked = checked::check(fake, std::span<const EntryView>(lines), at(Operation::append), chars, scratch);
    fixtures::MemoryView view;
    const std::optional<Refusal> r = refused_admit(view, checked, std::nullopt, chars);
    REQUIRE(r.has_value());
    CHECK(r->kind == Kind::entry_invalid);
    CHECK_EQ(r->code, std::string("parent"));
    CHECK_EQ(r->index, 0U);
    CHECK_EQ(r->id, std::string(id_of(b)));
}

// The target's rules the store leaves to checked, at append and import
// alike: C's target A of another scope, part 2's target; C its own target,
// part 1's id_mismatch (an id is its header's digest, the target in it).
QIVEN_CASE(store_stage, target_rules) {
    const auto bench = std::make_unique<Bench>(4U << 20U);
    const Book& book = bench->book;
    const Stored a = book.made("A", "A", *book.stored("A").content, [](auto& f) { f.scope = "project:other"; });
    const std::string target = id_of(a);
    const Stored c = book.made("C", "C", *book.stored("C").content, [&](auto& f) { f.target = target; });
    const std::vector<Stored> call = {c};
    const std::vector<EntryView> lines = views(call);
    const auto checked = checked::check(bench->fake, std::span<const EntryView>(lines), at(Operation::append),
                                        bench->chars, bench->scratch);
    fixtures::MemoryView view;
    hold(view, {a});
    const std::optional<Refusal> r = refused_admit(view, checked, std::nullopt, bench->chars);
    REQUIRE(r.has_value());
    CHECK_EQ(r->code + " " + r->id, "target " + id_of(c));
    hold(view, {c});
    const std::optional<Refusal> i = refused([&] {
        checked::import_entry(view, checked.entries[0].header(), c.content, 0, at(Operation::import_), bench->chars);
    });
    REQUIRE(i.has_value());
    CHECK_EQ(i->code + " " + i->id, "target " + id_of(c));
    Stored self = c;
    self.line.replace(self.line.find(target), target.size(), id_of(c));
    for (const Operation op : {Operation::append, Operation::import_}) {
        const std::optional<Refusal> p = refused_check(bench->fake, {self}, op, bench->chars);
        REQUIRE(p.has_value());
        CHECK_EQ(p->code, std::string("id_mismatch"));
    }
}

QIVEN_CASE(store_stage, maximum) {
    const auto bench = std::make_unique<Bench>(4U << 20U);
    Fake& fake = bench->fake;
    const Book& book = bench->book;
    const Chars chars = bench->chars;
    ScratchPages& scratch = bench->scratch;
    const std::vector<Stored> call = {book.stored("A")};
    const std::vector<EntryView> lines = views(call);
    const checked::Checked<Fake> checked = checked::check(fake, std::span<const EntryView>(lines), at(Operation::append), chars, scratch);
    Limits l;
    l.target_count = 2;
    l.warning_count = 2;
    l.max_count = 2;
    fixtures::MemoryView at_max;
    at_max.volume(100, 1);
    CHECK(!refused_admit(at_max, checked, std::nullopt, chars, l).has_value());
    fixtures::MemoryView by_count;
    by_count.volume(100, 2);
    const std::optional<Refusal> count = refused_admit(by_count, checked, std::nullopt, chars, l);
    REQUIRE(count.has_value());
    CHECK(count->kind == Kind::full);
    CHECK_EQ(count->code, std::string("store_full"));
    CHECK_EQ(count->count, 3U);
    CHECK_EQ(count->size, 100 + checked.size);
    Limits s;
    s.max_size = 100 + checked.size;
    s.warning_size = s.max_size;
    s.target_size = s.max_size;
    fixtures::MemoryView size_max;
    size_max.volume(100, 0);
    CHECK(!refused_admit(size_max, checked, std::nullopt, chars, s).has_value());
    size_max.volume(101, 0);
    const std::optional<Refusal> size = refused_admit(size_max, checked, std::nullopt, chars, s);
    REQUIRE(size.has_value());
    CHECK_EQ(size->code, std::string("store_full"));
}

QIVEN_CASE(store_stage, import) {
    const auto bench = std::make_unique<Bench>(4U << 20U);
    Fake& fake = bench->fake;
    const Book& book = bench->book;
    const Chars chars = bench->chars;
    ScratchPages& scratch = bench->scratch;
    // In (scope, lane, seq) order: implementer's lane, then reviewer's.
    const std::vector<Stored> all = {book.stored("A"),   book.stored("B"), book.stored("D"),
                                     book.stored("ACC"), book.stored("E"), book.stored("C"),
                                     book.stored("M1"),  book.stored("M2")};
    const std::vector<EntryView> lines = views(all);
    const checked::Checked<Fake> checked = checked::check(fake, std::span<const EntryView>(lines), at(Operation::import_), chars, scratch);
    CHECK_EQ(checked.entries.size(), all.size());
    const auto header_of = [&](std::size_t i) -> const qiven3::checked::Header& { return checked.entries[i].header(); };
    const auto content_at = [&](std::size_t i) {
        return all[i].content ? std::optional<std::string_view>(*all[i].content) : std::nullopt;
    };
    // As each line arrives, before its insert: the store's chain.
    // One refusal at a time, so that the case's frame stays within the bound.
    std::optional<Refusal> r;
    fixtures::MemoryView view;
    const auto arrive = [&](std::size_t i) {
        return refused(
            [&] { qiven3::store::stage::arrives(view, lines[i].keys, lines[i], i, at(Operation::import_)); });
    };
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(!arrive(i).has_value());
        view.put(header_of(i), content_at(i));
    }
    r = arrive(7);
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("parent"));
    CHECK_EQ(r->index, 7U);
    CHECK_EQ(r->id, id_of(all[7]));
    const auto gap = std::make_unique<fixtures::MemoryView>();
    gap->put(header_of(5), content_at(5));
    r = refused([&] { qiven3::store::stage::arrives(*gap, lines[7].keys, lines[7], 7, at(Operation::import_)); });
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("parent"));
    // Once every header is in, the final view: E's forget of B applied.
    view.forget(id_of(all[1]));
    const auto final_code = [&](fixtures::MemoryView& in, std::size_t i, std::optional<std::string_view> content) {
        return refused([&] {
            checked::import_entry(in, header_of(i), content, i, at(Operation::import_), chars);
        });
    };
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(!final_code(view, i, i == 1 ? std::nullopt : content_at(i)).has_value());
    }
    r = final_code(view, 0, std::nullopt);
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("body_missing"));
    r = final_code(view, 1, content_at(1));
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("forgotten_body"));
    CHECK_EQ(r->index, 1U);
    CHECK_EQ(r->id, id_of(all[1]));
    // Part 2's other rules, validator's: C's target A not held.
    const auto lacking = std::make_unique<fixtures::MemoryView>();
    for (std::size_t i = 1; i < all.size(); ++i) {
        lacking->put(header_of(i), content_at(i));
    }
    r = final_code(*lacking, 5, content_at(5));
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("target"));
    CHECK_EQ(r->index, 5U);
    // Part 2's other rules before forgotten_body: C forgotten, its body given.
    lacking->forget(id_of(all[5]));
    r = final_code(*lacking, 5, content_at(5));
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("target"));
    // The content read: M2's object not M1's, identity.
    const auto moved = std::make_unique<fixtures::MemoryView>();
    for (std::size_t i = 0; i < all.size(); ++i) {
        std::optional<std::string> content = all[i].content;
        if (i == 6) {
            const std::size_t at_path = content->find("qiven3/entry.py");
            REQUIRE(at_path != std::string::npos);
            content->replace(at_path, 15, "qiven3/other.py");
        }
        moved->put(header_of(i), content ? std::optional<std::string_view>(*content) : std::nullopt);
    }
    r = final_code(*moved, 7, content_at(7));
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("identity"));
    // A revision of the forgotten M1, itself not, forgotten_target before
    // identity; once forgotten too, a whole record, it stands.
    view.forget(id_of(all[6]));
    r = final_code(view, 7, content_at(7));
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("forgotten_target"));
    CHECK_EQ(r->index, 7U);
    view.forget(id_of(all[7]));
    CHECK(!final_code(view, 7, std::nullopt).has_value());
}

// The source's heads against those its entries give, in order.
QIVEN_CASE(store_stage, import_heads) {
    const auto bench = std::make_unique<Bench>(1U << 20U);
    const Book& book = bench->book;
    const std::vector<Stored> all = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
    std::optional<Refusal> r;
    std::vector<qiven3::store::LaneHead> source(2);
    source[0] = lane_head(qiven_scope, implementer_lane, id_of(all[1]), 2);
    source[1] = lane_head(qiven_scope, "reviewer", id_of(all[3]), 3);
    std::vector<qiven3::store::LaneHead> given = source;
    CHECK(!refused([&] { checked::same_heads(source, given, at(Operation::import_)); }).has_value());
    given[1].head.seq = 2;
    r = refused([&] { checked::same_heads(source, given, at(Operation::import_)); });
    REQUIRE(r.has_value());
    CHECK_EQ(r->code, std::string("head_mismatch"));
    CHECK_EQ(r->index, 1U);
    CHECK_EQ(r->id, std::string(id_of(all[3])));
    given.pop_back();
    r = refused([&] { checked::same_heads(source, given, at(Operation::import_)); });
    REQUIRE(r.has_value());
    CHECK_EQ(r->index, 1U);
}

// A ruled memory export's next: a forged line, key and body key, the store's two.
QIVEN_CASE(store_stage, memory_export) {
    using qiven3::store::state::Held;
    const auto bench = std::make_unique<Bench>(1U << 20U);
    const std::vector<Stored> list = {bench->book.stored("A")};
    const std::vector<EntryView> lines = views(list);
    const std::string id(lines.at(0).keys.id);
    const std::vector<std::pair<void (*)(Held&), std::string>> rows = {
        {[](Held& e) { e.line.assign("not a line"); }, "a header line the parser refuses"},
        {[](Held& e) { e.kind.assign("other"); }, "an entry's column differs from its line"},
        {[](Held& e) { e.content = e.has_body = false; }, id},
        {[](Held& e) { e.has_body = false; }, id},  // a body key with no record
        {[](Held& e) { e.content = false; }, id}};  // a record with no body key
    for (const auto& [forge, detail] : rows) {
        auto heap = bench->fake.pages(std::size_t{1} << 23U);
        const auto st = std::make_unique<qiven3::store::state::State>(heap.bytes(), Limits{});
        qiven3::checked::MemoryStore<Fake> ruled(*st, bench->fake);
        (void)ruled.append(lines.at(0).keys.scope, lines.at(0).keys.lane, std::nullopt, lines);
        forge(qiven3::store::state::Locked(*st, qiven3::store::state::Unbounded{}).data().held[0]);
        auto exported = ruled.export_(std::nullopt, {});
        qiven3::store::Line line(bench->arena);
        CHECK_EQ(refused([&] { (void)exported.next(line); }).value_or(Refusal{}).detail, detail);
    }
}
