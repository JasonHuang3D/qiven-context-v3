// validator's cases at import, against a view in memory: try_chain, a
// line as it arrives, parent and seq against its lane's head; try_final,
// an entry in the final view, every entry held: target, at_order,
// reference, forgotten_target, identity and map_evidence, the first
// deciding by the code's order; a second forget by the final view's first
// forget of its target; a revision of a forgotten target taken when
// itself forgotten (a whole record); a revision whose content is not
// given not read for identity; evaluated by the compiler on
// std::allocator. At run time, the fixtures imported whole, E's forget of
// B among them: each entry passes, its allocations within 256 KiB plus 8
// times its content and its revision's target's content, the arena left
// as it was; each allocation, failed in turn, a MemoryError with nothing
// held, the run completing past the last, which counts them exactly:
// each header the view gives 1 (a parent, a target, an entry referenced),
// a content's value 4, a revision with an identity its target's value 4
// and text 1: A 4, B 1 (forgotten, no content), C 5, D 5, ACC 6, E 2, M1
// 5, M2 11, ACC2 6, MX 5, L1 4, L2 7 (L1 its parent, target and
// reference); 61 in all. The same lines arriving in (lane, seq) order
// each pass try_chain before their insert, and a line arriving again is
// refused parent.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/header.hpp"
#include "entry/validator.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/filled.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/view.hpp"

namespace {

namespace validator = qiven3::entry::validator;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using fixtures::MemoryView;
using validator::Code;
using Slot = header::Slot;

static_assert(validator::FinalView<MemoryView, std::allocator<char>>);

constexpr std::string_view S = "project:qiven";
constexpr std::string_view L = "implementer";

// Ids: sha256: and 64 hex, the last two n's.
constexpr validator::Id make_id(std::size_t n) {
    constexpr std::string_view hex = "0123456789abcdef";
    std::string text = "sha256:" + fixtures::filled(62, '0');
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

constexpr std::array<std::string_view, 8> AT = {
    "2026-10-17T09:00:00.000Z", "2026-10-17T09:01:00.000Z", "2026-10-17T09:02:00.000Z", "2026-10-17T09:03:00.000Z",
    "2026-10-17T09:04:00.000Z", "2026-10-17T09:05:00.000Z", "2026-10-17T09:06:00.000Z", "2026-10-17T09:07:00.000Z"};

constexpr std::string_view CLAIM = R"({"claim":"x"})";
constexpr std::string_view DECISION = R"({"decides":"x"})";
constexpr std::string_view CHECK = R"({"method":"review","result":"verified"})";
constexpr std::string_view ITEM_A = R"({"item_id":"a","state":"open"})";
constexpr std::string_view ITEM_B = R"({"item_id":"b","state":"done"})";
constexpr std::string_view MAP_A = R"({"object":{"digest":"d","path":"a.py","repository":"r","type":"repo"}})";
constexpr std::string_view DIGEST = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
constexpr std::size_t NONE = SIZE_MAX;

// An entry n of a kind at minute m, seq, its parent and target by number
// or NONE, its content (none when empty) citing entry cite in evidence.
struct Spec {
    std::size_t n = 0;
    std::string_view kind = "claim";
    std::int64_t seq = 1;
    std::size_t parent = NONE;
    std::size_t minute = 0;
    std::size_t target = NONE;
    std::string_view content = CLAIM;
    std::size_t cite = NONE;
    std::string_view scope = S;
    std::string_view lane = L;
};

constexpr header::Header<> head_of(const Spec& s) {
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
    if (!s.content.empty()) {
        f.content = DIGEST;
    }
    f.id = I(s.n);
    return header::make(f);
}
constexpr std::string text_of(const Spec& s) {
    std::string text(s.content);
    if (s.cite != NONE) {
        // After claim and decides, in the canonical order.
        text.insert(text.size() - 1, R"(,"evidence":[{"id":")" + std::string(I(s.cite)) + R"(","type":"entry"}])");
    }
    return text;
}

// A held entry: put with its content, a forget's target forgotten.
constexpr void put(MemoryView& v, const Spec& s) {
    const std::string text = text_of(s);
    v.put(head_of(s), s.content.empty() ? std::nullopt : std::optional<std::string_view>(text));
    if (s.kind == "forget") {
        v.forget(I(s.target));
    }
}

// The final view: lane implementer of S holds 1 (a claim, forgotten by
// 5), 2 (an item), 3 (a map), 4 (a check of 1) and 5 (the forget); scope
// person holds 6; then each of more, in turn.
template <std::size_t N>
constexpr MemoryView final_view(const std::array<Spec, N>& more) {
    MemoryView v;
    put(v, Spec{.n = 1});
    put(v, Spec{.n = 2, .kind = "item", .seq = 2, .parent = 1, .minute = 1, .content = ITEM_A});
    put(v, Spec{.n = 3, .kind = "map", .seq = 3, .parent = 2, .minute = 2, .content = MAP_A});
    put(v, Spec{.n = 4, .kind = "check", .seq = 4, .parent = 3, .minute = 3, .target = 1, .content = CHECK});
    put(v, Spec{.n = 5, .kind = "forget", .seq = 5, .parent = 4, .minute = 4, .target = 1, .content = {}});
    put(v, Spec{.n = 6, .scope = "person"});
    for (const Spec& s : more) {
        put(v, s);
    }
    return v;
}

// try_final of more's entry at index i, given its content unless erased
// or withheld; -1 for no fault.
template <std::size_t N>
constexpr int final_code(const std::array<Spec, N>& more, std::size_t i, bool given = true) {
    MemoryView v = final_view(more);
    const Spec& s = more.at(i);
    const std::string text = text_of(s);
    const header::Header<> h = head_of(s);
    std::string held;
    const bool has = given && v.body(I(s.n), held);
    const std::optional<Code> c =
        validator::try_final(v, h, has ? std::optional<std::string_view>(text) : std::nullopt);
    return c ? static_cast<int>(*c) : -1;
}
constexpr int code(Code c) { return static_cast<int>(c); }

// A next entry n of the lane: seq 6 on 5 at minute 5.
constexpr Spec next(std::size_t n, std::string_view kind = "claim", std::size_t target = NONE,
                    std::string_view content = CLAIM) {
    return Spec{.n = n, .kind = kind, .seq = 6, .parent = 5, .minute = 5, .target = target, .content = content};
}
// Spec s in lane lane at seq, on parent, at minute.
constexpr Spec on(Spec s, std::string_view lane, std::int64_t seq, std::size_t parent, std::size_t minute) {
    s.lane = lane;
    s.seq = seq;
    s.parent = parent;
    s.minute = minute;
    return s;
}
constexpr Spec citing(Spec s, std::size_t id) {
    s.cite = id;
    return s;
}

// Valid: a root; the import's entries held whatever their lane's order, a
// check of an entry of a lane imported later; a check of the forgotten 1;
// a forget, the first of its target; a whole record forgotten; an item's
// revision keeping its identity; a revision whose content is withheld.
static_assert(final_code(std::array{next(10)}, 0) == -1);
static_assert(final_code(std::array{on(next(10, "check", 11, CHECK), "a", 1, NONE, 6), on(next(11), "z", 1, NONE, 5)},
                         0) == -1);
static_assert(final_code(std::array{next(10, "check", 1, CHECK)}, 0) == -1);
static_assert(final_code(std::array{next(10), on(next(11, "forget", 10, {}), L, 7, 10, 6)}, 1) == -1);
static_assert(final_code(std::array{next(10), on(next(11, "claim", 10), L, 7, 10, 6),
                                    on(next(12, "forget", 11, {}), L, 8, 11, 7),
                                    on(next(13, "forget", 10, {}), "other", 1, NONE, 7)},
                         1) == -1);
static_assert(final_code(std::array{next(10, "item", NONE, ITEM_B), on(next(11, "item", 10, ITEM_B), L, 7, 10, 6)},
                         1) == -1);
static_assert(final_code(std::array{next(10, "item", NONE, ITEM_B), on(next(11, "item", 10, ITEM_A), L, 7, 10, 6)}, 1,
                         false) == -1);

// parent: a parent the view does not hold.
static_assert(final_code(std::array{on(next(10), L, 6, 20, 5)}, 0) == code(Code::parent));
// target: not held; another scope; a revision of another kind; a check of
// a change kind; a forget of a forget; a second forget, its first not.
static_assert(final_code(std::array{next(10, "claim", 20)}, 0) == code(Code::target));
static_assert(final_code(std::array{next(10, "claim", 6)}, 0) == code(Code::target));
static_assert(final_code(std::array{next(10, "claim", 2)}, 0) == code(Code::target));
static_assert(final_code(std::array{next(10, "check", 4, CHECK)}, 0) == code(Code::target));
static_assert(final_code(std::array{next(10, "forget", 5, {})}, 0) == code(Code::target));
static_assert(final_code(std::array{next(10, "forget", 2, {}), on(next(11, "forget", 2, {}), "other", 1, NONE, 6)},
                         1) == code(Code::target));
static_assert(final_code(std::array{next(10, "forget", 2, {}), on(next(11, "forget", 2, {}), "other", 1, NONE, 6)},
                         0) == -1);
static_assert(final_code(std::array{next(10, "forget", 1, {})}, 0) == code(Code::target));
// at_order: earlier than the parent's; not later than the target's.
static_assert(final_code(std::array{on(next(10), L, 6, 5, 3)}, 0) == code(Code::at_order));
static_assert(final_code(std::array{on(next(10, "check", 11, CHECK), "a", 1, NONE, 5), on(next(11), "z", 1, NONE, 5)},
                         0) == code(Code::at_order));
// reference: not held; another scope.
static_assert(final_code(std::array{citing(next(10), 20)}, 0) == code(Code::reference));
static_assert(final_code(std::array{citing(next(10), 6)}, 0) == code(Code::reference));
// forgotten_target: a revision of the forgotten 1, itself not; its target's
// body not given.
static_assert(final_code(std::array{next(10, "claim", 1)}, 0) == code(Code::forgotten_target));
static_assert(final_code(std::array{next(10, "item", NONE, {}), on(next(11, "item", 10, ITEM_B), L, 7, 10, 6)}, 1) ==
              code(Code::forgotten_target));
// identity: an item's revision changing its item_id.
static_assert(final_code(std::array{next(10, "item", NONE, ITEM_B), on(next(11, "item", 10, ITEM_A), L, 7, 10, 6)},
                         1) == code(Code::identity));
// map_evidence: a decision citing the map 3.
static_assert(final_code(std::array{citing(next(10, "decision", NONE, DECISION), 3)}, 0) == code(Code::map_evidence));
// The first deciding: target before at_order; at_order before reference;
// reference before forgotten_target; forgotten_target before identity.
static_assert(final_code(std::array{on(next(10, "claim", 20), L, 6, 5, 3)}, 0) == code(Code::target));
static_assert(final_code(std::array{citing(on(next(10), L, 6, 5, 3), 20)}, 0) == code(Code::at_order));
static_assert(final_code(std::array{citing(next(10, "claim", 1), 20)}, 0) == code(Code::reference));
static_assert(final_code(std::array{next(10, "item", NONE, ITEM_B), next(11, "forget", 10, {}),
                                    on(next(12, "item", 10, ITEM_A), "other", 1, NONE, 6)},
                         2) == code(Code::forgotten_target));

// try_chain of each line of more as it arrives, before its insert: the
// first fault's index * 10 plus its code, or -1.
template <std::size_t N>
constexpr int chain_code(const std::array<Spec, N>& more) {
    MemoryView v = final_view(std::array<Spec, 0>{});
    for (std::size_t i = 0; i < N; ++i) {
        if (const std::optional<Code> c = validator::try_chain(v, head_of(more.at(i)))) {
            return static_cast<int>(i * 10) + static_cast<int>(*c);
        }
        put(v, more.at(i));
    }
    return -1;
}

// Valid: the lane's next; a root in an empty lane; a chain arriving.
static_assert(chain_code(std::array{next(10)}) == -1);
static_assert(chain_code(std::array{on(next(10), "other", 1, NONE, 5)}) == -1);
static_assert(chain_code(std::array{next(10), on(next(11), L, 7, 10, 6), on(next(12), "other", 1, NONE, 6)}) == -1);
// parent: not the lane's head; a parent in an empty lane; none in a held
// lane. seq: not the head's plus 1; not 1 in an empty lane. Each at its
// line.
static_assert(chain_code(std::array{on(next(10), L, 6, 4, 5)}) == code(Code::parent));
static_assert(chain_code(std::array{on(next(10), "other", 1, 5, 5)}) == code(Code::parent));
static_assert(chain_code(std::array{on(next(10), L, 1, NONE, 5)}) == code(Code::parent));
static_assert(chain_code(std::array{next(10), on(next(11), L, 7, 5, 6)}) == 10 + code(Code::parent));
static_assert(chain_code(std::array{on(next(10), L, 7, 5, 5)}) == code(Code::seq));
static_assert(chain_code(std::array{on(next(10), "other", 2, NONE, 5)}) == code(Code::seq));
static_assert(chain_code(std::array{next(10), on(next(11), L, 8, 10, 6)}) == 10 + code(Code::seq));

}  // namespace

namespace {

using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using Line = header::Header<Allocator<char>>;

// The fixtures imported whole: each entry held, E's forget of B applied;
// each entry's header on an arena, its content unless forgotten, and its
// allocations.
struct Imported {
    std::string_view name;
    Line head;
    std::optional<std::string_view> content;
    std::size_t bytes = 0;
    std::size_t allocations = 0;
};

struct Setup {
    MemoryView view;
    std::vector<Imported> entries;
};

constexpr std::array<std::pair<std::string_view, std::size_t>, 12> ALLOCATIONS = {{{"A", 4},
                                                                                  {"B", 1},
                                                                                  {"C", 5},
                                                                                  {"D", 5},
                                                                                  {"ACC", 6},
                                                                                  {"E", 2},
                                                                                  {"M1", 5},
                                                                                  {"M2", 11},
                                                                                  {"ACC2", 6},
                                                                                  {"MX", 5},
                                                                                  {"L1", 4},
                                                                                  {"L2", 7}}};

constexpr std::size_t budget_base = 262144;

void setup(const canon::Basic<>& v, Setup& s, Arena& arena) {
    for (const auto& [name, allocations] : ALLOCATIONS) {
        const fixtures::EntryView e = *fixtures::find_entry(v, name);
        s.view.put(header::parse_line(e.header), e.content);
        s.entries.push_back(Imported{name, header::parse_line(e.header, Allocator<char>(arena)), e.content, 0,
                                     allocations});
    }
    for (Imported& i : s.entries) {
        if (i.head.value(Slot::kind) == "forget") {
            s.view.forget(*i.head.text(Slot::target));
        }
    }
    for (Imported& i : s.entries) {
        if (s.view.forgotten(i.head.value(Slot::id))) {
            i.content.reset();
        }
        i.bytes = i.content ? i.content->size() : 0;
        if (i.name == "M2") {
            i.bytes += fixtures::find_entry(v, "M1")->content->size();
        }
    }
}

}  // namespace

QIVEN3_CASE(validator, import_budget) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<std::byte> pages(1U << 20U);
    Arena items(pages);
    Setup s;
    setup(v, s, items);
    std::size_t total = 0;
    for (const Imported& i : s.entries) {
        Arena& arena = qiven3_ctx.counted.arena(budget_base + 8 * i.bytes);
        const std::size_t used = arena.used();
        const std::size_t before = arena.peak();
        CHECK(!validator::try_final(s.view, i.head, i.content, Allocator<char>(arena)));
        CHECK_EQ(arena.used(), used);
        CHECK(arena.peak() - before <= budget_base + 8 * i.bytes);
        std::cout << "validator.import_budget: " << i.name << ": " << arena.peak() - before << " bytes, within "
                  << budget_base + 8 * i.bytes << "\n";
        total += i.allocations;
    }
    CHECK_EQ(total, 61U);
    // The same lines arriving in (lane, seq) order, each passing the chain
    // rules before its insert; a line arriving twice refused parent.
    std::vector<const Imported*> order;
    for (const Imported& i : s.entries) {
        order.push_back(&i);
    }
    std::sort(order.begin(), order.end(), [](const Imported* a, const Imported* b) {
        return std::pair(a->head.value(Slot::lane), a->head.seq()) < std::pair(b->head.value(Slot::lane), b->head.seq());
    });
    MemoryView arriving;
    for (const Imported* i : order) {
        CHECK(!validator::try_chain(arriving, i->head));
        arriving.put(i->head, i->content);
    }
    CHECK(validator::try_chain(arriving, order.back()->head) == std::optional<Code>(Code::parent));
    s.entries.clear();
}

QIVEN3_CASE(validator, import_faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<std::byte> item_pages(1U << 20U);
    std::vector<std::byte> pages(budget_base + 8 * 65536);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    bool done = false;
    // Every buffer and value given back.
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    Arena items(item_pages);
    Setup s;
    setup(v, s, items);
    for (const Imported& i : s.entries) {
        const auto op = [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            done = !validator::try_final(s.view, i.head, i.content, Allocator<char>(*arena));
            arena->attach(nullptr);
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "validator.try_final", countdown, op, empty);
        std::cout << "validator.import_faults: " << i.name << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, i.allocations);
        CHECK_EQ(runs.completed_at, i.allocations + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        done = false;
    }
    s.entries.clear();
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
