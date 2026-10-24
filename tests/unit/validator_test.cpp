// validator's cases: part 2's codes against a view in memory, each on its
// own entry and the first deciding in the order parent, seq, target,
// at_order, reference, forgotten_target, identity, map_evidence, entry by
// entry; the call's earlier entries held beside the view; evaluated by
// the compiler on std::allocator over entries built here. At run time:
// the fixtures' lanes appended in turn, valid, E's forget of B among them;
// then a revision of B forgotten_target, a second forget of B, a map's
// revision changing its object, a decision citing M1, a reference out of
// scope and not held; and each fault thrown as RuleError.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "entry/validator.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/view.hpp"

namespace {

namespace validator = qiven3::entry::validator;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using fixtures::Item;
using fixtures::MemoryView;
using validator::Code;
using validator::RuleError;
using Slot = header::Slot;

static_assert(validator::View<MemoryView, std::allocator<char>>);
static_assert(validator::Appended<Item<>, std::allocator<char>>);

constexpr std::string_view S = "project:qiven";
constexpr std::string_view L = "implementer";

// Ids: sha256: and 64 hex, the last two n's.
constexpr validator::Id make_id(std::size_t n) {
    constexpr std::string_view hex = "0123456789abcdef";
    std::string text = "sha256:" + std::string(62, '0');
    text.push_back(hex.at((n / 16) % 16));
    text.push_back(hex.at(n % 16));
    return validator::Id::of(text);
}
constexpr std::array<validator::Id, 24> IDS = [] {
    std::array<validator::Id, 24> ids{};
    for (std::size_t n = 0; n < ids.size(); ++n) {
        ids.at(n) = make_id(n);
    }
    return ids;
}();
constexpr std::string_view I(std::size_t n) { return IDS.at(n).view(); }

constexpr std::array<std::string_view, 10> AT = {
    "2026-10-17T09:00:00.000Z", "2026-10-17T09:01:00.000Z", "2026-10-17T09:02:00.000Z", "2026-10-17T09:03:00.000Z",
    "2026-10-17T09:04:00.000Z", "2026-10-17T09:05:00.000Z", "2026-10-17T09:06:00.000Z", "2026-10-17T09:07:00.000Z",
    "2026-10-17T09:08:00.000Z", "2026-10-17T09:09:00.000Z"};

constexpr std::string_view CLAIM = R"({"claim":"x"})";
constexpr std::string_view DECISION = R"({"decides":"x"})";
constexpr std::string_view CHECK = R"({"method":"review","result":"verified"})";
constexpr std::string_view ITEM_A = R"({"item_id":"a","state":"open"})";
constexpr std::string_view ITEM_B = R"({"item_id":"b","state":"done"})";
constexpr std::string_view MAP_A = R"({"object":{"digest":"d","path":"a.py","repository":"r","type":"repo"}})";
constexpr std::string_view MAP_B = R"({"object":{"digest":"d","path":"b.py","repository":"r","type":"repo"}})";
constexpr std::size_t NONE = SIZE_MAX;

// An entry n of a kind at minute m, seq, its parent and target by number
// or NONE, its content (none when empty) citing entry cite in field.
struct Spec {
    std::size_t n = 0;
    std::string_view kind = "claim";
    std::int64_t seq = 1;
    std::size_t parent = NONE;
    std::size_t minute = 0;
    std::size_t target = NONE;
    std::string_view content = CLAIM;
    std::size_t cite = NONE;
    std::string_view field = "evidence";
    std::string_view scope = S;
    std::string_view lane = L;
};

constexpr Item<> item(const Spec& s) {
    header::FieldsView f;
    f.scope = s.scope;
    f.lane = s.lane;
    f.seq = s.seq;
    if (s.parent != NONE) {
        f.parent = I(s.parent);
    }
    f.kind = s.kind;
    f.writer = "implementer/s39";
    f.basis = "claimed";
    f.at = AT.at(s.minute);
    if (s.target != NONE) {
        f.target = I(s.target);
    }
    f.id = I(s.n);
    Item<> e{header::make(f), std::nullopt};
    if (!s.content.empty()) {
        std::string text(s.content);
        if (s.cite != NONE && s.field == "object") {
            text = R"({"object":{"digest":"d","id":")" + std::string(I(s.cite)) + R"(","type":"entry"}})";
        } else if (s.cite != NONE) {
            // The member in its canonical place: first, or last.
            const std::string member =
                "\"" + std::string(s.field) + "\":[{\"id\":\"" + std::string(I(s.cite)) + "\",\"type\":\"entry\"}]";
            if (text.substr(2, s.field.size()) > s.field) {
                text.insert(1, member + ",");
            } else {
                text.insert(text.size() - 1, "," + member);
            }
        }
        e.text = text;
    }
    return e;
}

constexpr void put(MemoryView& v, const Spec& s) {
    const Item<> e = item(s);
    v.put(e.header(), e.has_body() ? std::optional<std::string_view>(*e.text) : std::nullopt);
}

// The view: lane implementer of S holds 1 (a claim, forgotten by 5), 2
// (an item), 3 (a map), 4 (a check of 1) and 5 (the forget), its head 5
// at seq 5, minute 4; scope person holds 6, a claim.
constexpr MemoryView base() {
    MemoryView v;
    put(v, Spec{.n = 1});
    put(v, Spec{.n = 2, .kind = "item", .seq = 2, .parent = 1, .minute = 1, .content = ITEM_A});
    put(v, Spec{.n = 3, .kind = "map", .seq = 3, .parent = 2, .minute = 2, .content = MAP_A});
    put(v, Spec{.n = 4, .kind = "check", .seq = 4, .parent = 3, .minute = 3, .target = 1, .content = CHECK});
    put(v, Spec{.n = 5, .kind = "forget", .seq = 5, .parent = 4, .minute = 4, .target = 1, .content = {}});
    v.forget(I(1));
    put(v, Spec{.n = 6, .scope = "person"});
    return v;
}

// A call's first fault, as (code, index), against base(); -1 for none.
struct Got {
    int code = -1;
    std::size_t index = 0;
    constexpr bool operator==(const Got&) const = default;
};
template <std::size_t N>
constexpr Got run(const std::array<Spec, N>& call, std::optional<std::size_t> expected = 5,
                  std::string_view lane = L) {
    MemoryView v = base();
    std::vector<Item<>> items;
    for (const Spec& s : call) {
        items.push_back(item(s));
    }
    const std::optional<std::string_view> head =
        expected ? std::optional<std::string_view>(I(*expected)) : std::nullopt;
    const std::optional<RuleError> e = validator::try_validate(v, S, lane, head, std::span<const Item<>>(items));
    if (!e) {
        return Got{};
    }
    if (e->id.view() != I(call.at(e->index).n)) {
        return Got{-2, e->index};
    }
    return Got{static_cast<int>(e->code), e->index};
}
constexpr Got got(Code c, std::size_t index = 0) { return Got{static_cast<int>(c), index}; }
constexpr Got NO_FAULT{};

// A next entry n of the lane: seq 6 on 5 at minute 5, or seq 7 on 6.
constexpr Spec next(std::size_t n, std::string_view kind = "claim", std::size_t target = NONE,
                    std::string_view content = CLAIM) {
    return Spec{.n = n, .kind = kind, .seq = 6, .parent = 5, .minute = 5, .target = target, .content = content};
}
constexpr Spec after(Spec s, std::size_t parent, std::int64_t seq = 7, std::size_t minute = 6) {
    s.parent = parent;
    s.seq = seq;
    s.minute = minute;
    return s;
}
constexpr Spec cite(Spec s, std::size_t id, std::string_view field = "evidence") {
    s.cite = id;
    s.field = field;
    return s;
}
constexpr Spec in(Spec s, std::string_view scope, std::string_view lane = L) {
    s.scope = scope;
    s.lane = lane;
    return s;
}

// Valid: a root; a chain; a check of the forgotten 1 by its header; a
// root in an empty lane; a revision kept in the call; references held.
static_assert(run(std::array{next(10)}) == NO_FAULT);
static_assert(run(std::array{next(10), after(next(11), 10)}) == NO_FAULT);
static_assert(run(std::array{next(10, "check", 1, CHECK)}) == NO_FAULT);
static_assert(run(std::array{Spec{.n = 10, .lane = "other"}}, std::nullopt, "other") == NO_FAULT);
static_assert(run(std::array{next(10, "item", NONE, ITEM_B), after(next(11, "item", 10, ITEM_B), 10)}) == NO_FAULT);
static_assert(run(std::array{next(10), after(cite(next(11), 10), 10), after(cite(next(12), 2, "source"), 11, 8, 7)}) ==
              NO_FAULT);
static_assert(run(std::array{next(10, "map", 3, MAP_A)}) == NO_FAULT);
static_assert(run(std::array{cite(next(10), 3)}) == NO_FAULT);
static_assert(run(std::array{cite(next(10, "decision", NONE, DECISION), 3, "source")}) == NO_FAULT);

// parent: not the expected head; the view's head not the expected one;
// another scope or lane; a later entry's not the previous.
static_assert(run(std::array{after(next(10), 4, 6, 5)}) == got(Code::parent));
static_assert(run(std::array{next(10)}, 4) == got(Code::parent));
static_assert(run(std::array{after(next(10), 4, 5, 5)}, 4) == got(Code::parent));
static_assert(run(std::array{in(next(10), "person")}) == got(Code::parent));
static_assert(run(std::array{in(next(10), S, "other")}) == got(Code::parent));
static_assert(run(std::array{next(10), after(next(11), 5)}) == got(Code::parent, 1));
static_assert(run(std::array{Spec{.n = 10, .lane = "other"}}, 5, "other") == got(Code::parent));

// seq: not the parent's plus 1; 1 with none.
static_assert(run(std::array{after(next(10), 5, 7, 5)}) == got(Code::seq));
static_assert(run(std::array{next(10), after(next(11), 10, 8)}) == got(Code::seq, 1));
static_assert(run(std::array{Spec{.n = 10, .seq = 2, .lane = "other"}}, std::nullopt, "other") == got(Code::seq));

// target: not held; another scope; a later entry of the call; a revision
// of another kind; a check or accept of a change kind; a retire of a
// retire or a forget; a forget of a forget; a second forget, the view's
// or the call's.
static_assert(run(std::array{next(10, "claim", 20)}) == got(Code::target));
static_assert(run(std::array{next(10, "claim", 6)}) == got(Code::target));
static_assert(run(std::array{next(10, "claim", 11), after(next(11), 10)}) == got(Code::target));
static_assert(run(std::array{next(10, "claim", 2)}) == got(Code::target));
static_assert(run(std::array{next(10, "check", 4, CHECK)}) == got(Code::target));
static_assert(run(std::array{next(10, "accept", 5, CHECK)}) == got(Code::target));
static_assert(run(std::array{next(10, "retire", 5)}) == got(Code::target));
static_assert(run(std::array{next(10, "retire", 4), after(next(11, "retire", 10), 10)}) == got(Code::target, 1));
static_assert(run(std::array{next(10, "forget", 5, {})}) == got(Code::target));
static_assert(run(std::array{next(10, "forget", 1, {})}) == got(Code::target));
static_assert(run(std::array{next(10, "forget", 2, {}), after(next(11, "forget", 2, {}), 10)}) ==
              got(Code::target, 1));
static_assert(run(std::array{next(10, "forget", 2, {}), after(next(11, "forget", 3, {}), 10)}) == NO_FAULT);
static_assert(run(std::array{next(10, "retire", 4)}) == NO_FAULT);

// at_order: earlier than the parent's; not later than the target's; equal
// to the parent's holds.
static_assert(run(std::array{after(next(10), 5, 6, 3)}) == got(Code::at_order));
static_assert(run(std::array{after(next(10), 5, 6, 4)}) == NO_FAULT);
static_assert(run(std::array{next(10), after(next(11, "check", 10, CHECK), 10, 7, 5)}) == got(Code::at_order, 1));

// reference: not held, another scope, a later entry of the call, in
// evidence, source or a map's object.
static_assert(run(std::array{cite(next(10), 20)}) == got(Code::reference));
static_assert(run(std::array{cite(next(10), 6, "source")}) == got(Code::reference));
static_assert(run(std::array{cite(next(10), 11), after(next(11), 10)}) == got(Code::reference));
static_assert(run(std::array{cite(next(10, "map"), 20, "object")}) == got(Code::reference));
static_assert(run(std::array{cite(next(10, "map"), 2, "object")}) == NO_FAULT);
static_assert(run(std::array{cite(next(10, "claim"), 20, "object")}) == NO_FAULT);

// forgotten_target: a revision of the forgotten 1; of the call's entry
// with no body, or one an earlier forget of the call takes.
static_assert(run(std::array{next(10, "claim", 1)}) == got(Code::forgotten_target));
static_assert(run(std::array{next(10, "claim", NONE, {}), after(next(11, "claim", 10), 10)}) ==
              got(Code::forgotten_target, 1));
static_assert(run(std::array{next(10), after(next(11, "forget", 10, {}), 10),
                             after(next(12, "claim", 10), 11, 8, 7)}) == got(Code::forgotten_target, 2));

// identity: an item's id, a map's object, the view's target or the call's.
static_assert(run(std::array{next(10, "item", 2, ITEM_B)}) == got(Code::identity));
static_assert(run(std::array{next(10, "item", 2, ITEM_A)}) == NO_FAULT);
static_assert(run(std::array{next(10, "map", 3, MAP_B)}) == got(Code::identity));
static_assert(run(std::array{next(10, "item", NONE, ITEM_A), after(next(11, "item", 10, ITEM_B), 10)}) ==
              got(Code::identity, 1));
static_assert(run(std::array{cite(next(10, "map"), 2, "object"), after(cite(next(11, "map", 10), 4, "object"), 10)}) ==
              got(Code::identity, 1));

// map_evidence: a decision or check whose evidence names a map, the
// view's or the call's.
static_assert(run(std::array{cite(next(10, "decision", NONE, DECISION), 3)}) == got(Code::map_evidence));
static_assert(run(std::array{cite(next(10, "check", 2, CHECK), 3)}) == got(Code::map_evidence));
static_assert(run(std::array{next(10, "map", NONE, MAP_B), after(cite(next(11, "decision", NONE, DECISION), 10), 10)}) ==
              got(Code::map_evidence, 1));

// The first fault decides: within an entry by the order, then entry by
// entry.
static_assert(run(std::array{after(next(10, "claim", 20), 4, 9, 0)}) == got(Code::parent));
static_assert(run(std::array{after(next(10, "claim", 20), 5, 9, 0)}) == got(Code::seq));
static_assert(run(std::array{after(next(10, "claim", 20), 5, 6, 0)}) == got(Code::target));
static_assert(run(std::array{cite(after(next(10, "claim", 1), 5, 6, 0), 20)}) == got(Code::at_order));
static_assert(run(std::array{cite(next(10, "claim", 1), 20)}) == got(Code::reference));
static_assert(run(std::array{cite(next(10, "item", 2, ITEM_B), 6)}) == got(Code::reference));
static_assert(run(std::array{next(10, "decision", NONE, {}), after(cite(next(11, "decision", 10, DECISION), 3), 10)}) ==
              got(Code::forgotten_target, 1));
static_assert(run(std::array{cite(next(10, "check", 2, CHECK), 3), after(next(11), 4)}) == got(Code::map_evidence));

}  // namespace

namespace {

// A fixture entry by name as an item of a call.
Item<> fixture(const canon::Basic<>& v, std::string_view name) {
    const std::optional<fixtures::EntryView> e = fixtures::find_entry(v, name);
    if (!e) {
        std::cout << "validator: no fixture " << name << "\n";
        return Item<>{header::Header<>(), std::nullopt};
    }
    Item<> it{header::parse_line(e->header), std::nullopt};
    if (e->content) {
        it.text = std::string(*e->content);
    }
    return it;
}

// An entry made on a lane's head: its kind, at, target and content, its id
// computed.
Item<> made(MemoryView& view, std::string_view scope, std::string_view lane, std::string_view kind,
            std::string_view at, std::optional<std::string_view> target, std::optional<std::string> content) {
    const std::optional<validator::Head> head = view.head(scope, lane);
    header::FieldsView f;
    f.scope = scope;
    f.lane = lane;
    f.seq = head ? head->seq + 1 : 1;
    if (head) {
        f.parent = head->id.view();
    }
    f.kind = kind;
    f.writer = "reviewer/s70";
    f.basis = "claimed";
    f.at = at;
    f.target = target;
    return Item<>{header::make(f), std::move(content)};
}

// A call on its lane's head; each entry held once the call passes, a
// forget's target forgotten.
std::optional<RuleError> append(MemoryView& view, std::vector<Item<>>& items) {
    const header::Header<>& first = items.at(0).header();
    const std::string scope(first.value(Slot::scope));
    const std::string lane(first.value(Slot::lane));
    const std::optional<validator::Head> head = view.head(scope, lane);
    const std::optional<std::string_view> expected =
        head ? std::optional<std::string_view>(head->id.view()) : std::nullopt;
    const std::optional<RuleError> e =
        validator::try_validate(view, scope, lane, expected, std::span<const Item<>>(items));
    if (!e) {
        for (const Item<>& it : items) {
            view.put(it.header(), it.has_body() ? std::optional<std::string_view>(*it.text) : std::nullopt);
            if (it.header().value(Slot::kind) == "forget") {
                view.forget(it.header().value(Slot::target));
            }
        }
    }
    return e;
}
std::optional<RuleError> append(MemoryView& view, const canon::Basic<>& v,
                                 std::initializer_list<std::string_view> names) {
    std::vector<Item<>> items;
    for (const std::string_view n : names) {
        items.push_back(fixture(v, n));
    }
    return append(view, items);
}
std::optional<RuleError> append(MemoryView& view, Item<> one) {
    std::vector<Item<>> items;
    items.push_back(std::move(one));
    return append(view, items);
}

std::string id_of(const canon::Basic<>& v, std::string_view name) {
    return std::string(fixture(v, name).header().value(Slot::id));
}

// The fixtures' lanes appended in turn: implementer's A, B, D and ACC;
// reviewer's C, M1, M2, ACC2 and MX; learner's L1 and L2; then E.
MemoryView lanes(const canon::Basic<>& v) {
    MemoryView view;
    for (const auto& call : {std::initializer_list<std::string_view>{"A", "B", "D", "ACC"},
                             {"C", "M1", "M2", "ACC2", "MX"},
                             {"L1", "L2"},
                             {"E"}}) {
        if (const std::optional<RuleError> e = append(view, v, call)) {
            std::cout << "validator.lanes: " << validator::name(e->code) << " at " << e->index << "\n";
        }
    }
    return view;
}

bool is(const std::optional<RuleError>& e, Code code) {
    if (!e) {
        std::cout << "validator: no fault, " << validator::name(code) << " wanted\n";
        return false;
    }
    std::cout << "validator: " << validator::name(e->code) << " at " << e->index << ", " << e->id.view() << "\n";
    return e->code == code && e->index == 0;
}

}  // namespace

QIVEN3_CASE(validator, fixtures) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    MemoryView view;
    CHECK(!append(view, v, {"A", "B", "D", "ACC"}));
    CHECK(!append(view, v, {"C", "M1", "M2", "ACC2", "MX"}));
    CHECK(!append(view, v, {"L1", "L2"}));
    CHECK(!append(view, v, {"E"}));
    CHECK(view.forgotten(id_of(v, "B")));
    // Out of the order the chain gives: L2 before L1, and C before A.
    MemoryView empty;
    CHECK(is(append(empty, v, {"L2"}), Code::parent));
    CHECK(is(append(empty, v, {"C"}), Code::target));
}

QIVEN3_CASE(validator, refusals) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    MemoryView view = lanes(v);
    const std::string a = std::string(*fixtures::find_entry(v, "A")->content);
    const std::string b = id_of(v, "B");
    const std::string m1 = id_of(v, "M1");
    // A revision of B, whose body E forgot.
    CHECK(is(append(view, made(view, S, L, "claim", "2026-10-17T11:05:00.000Z", b, a)), Code::forgotten_target));
    // A second forget of B.
    CHECK(is(append(view, made(view, S, L, "forget", "2026-10-17T11:05:00.000Z", b, std::nullopt)), Code::target));
    // M2's revision mapping another file.
    std::string other = std::string(*fixtures::find_entry(v, "M2")->content);
    const std::size_t at = other.find("\"path\":\"");
    CHECK(at != std::string::npos);
    other.replace(at, 8, "\"path\":\"other/");
    CHECK(is(append(view, made(view, S, "reviewer", "map", "2026-10-17T12:30:00.000Z", id_of(v, "M2"), other)),
             Code::identity));
    // M2's revision mapping the same: held.
    const std::string same = std::string(*fixtures::find_entry(v, "M2")->content);
    CHECK(!append(view, made(view, S, "reviewer", "map", "2026-10-17T12:30:00.000Z", id_of(v, "M2"), same)));
    // A decision citing M1 in its evidence.
    const std::string cites = R"({"decides":"x","evidence":[{"id":")" + m1 + R"(","type":"entry"}]})";
    CHECK(is(append(view, made(view, S, "reviewer", "decision", "2026-10-17T12:40:00.000Z", std::nullopt, cites)),
             Code::map_evidence));
    // A claim citing A from another scope, and an id no entry has.
    const std::string a_id = id_of(v, "A");
    const std::string from = R"({"claim":"x","evidence":[{"id":")" + a_id + R"(","type":"entry"}]})";
    CHECK(is(append(view, made(view, "person", "reviewer", "claim", "2026-10-17T12:40:00.000Z", std::nullopt, from)),
             Code::reference));
    std::string none = from;
    none.replace(none.find(a_id), a_id.size(), std::string(I(20)));
    CHECK(is(append(view, made(view, S, "reviewer", "claim", "2026-10-17T12:40:00.000Z", std::nullopt, none)),
             Code::reference));
}

QIVEN3_CASE(validator, raise) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    MemoryView view = lanes(v);
    std::vector<Item<>> items;
    items.push_back(made(view, S, L, "claim", "2026-10-17T10:00:00.000Z", std::nullopt, std::string(CLAIM)));
    const std::string id(items.at(0).header().value(Slot::id));
    const std::string head(view.head(S, L)->id.view());
    bool thrown = false;
    try {
        validator::validate(view, S, L, std::optional<std::string_view>(head), std::span<const Item<>>(items));
    } catch (const RuleError& e) {
        thrown = true;
        CHECK_EQ(validator::name(e.code), std::string_view("at_order"));
        CHECK_EQ(e.index, 0U);
        CHECK_EQ(e.id.view(), std::string_view(id));
    }
    CHECK(thrown);
    // Nothing to validate: no fault and no read.
    const std::size_t reads = view.reads();
    validator::validate(view, S, L, std::nullopt, std::span<const Item<>>());
    CHECK_EQ(view.reads(), reads);
}
