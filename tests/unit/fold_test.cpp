// fold's cases: the order by (at, lane, seq); one writer; the fold's steps
// each ending it in turn, the first deciding: versions, withdrawn, leaves
// (F51, F118's fork as meant), current, forgotten, expired and the refuted
// row (F50 as F119 narrows it: a review's refutation, the own writer's and
// a transcript unverified leave it served); "active" through them; entries
// that take no part. Each is evaluated by the compiler on std::allocator.
// At run time: the fold's faults; FA2's status change over
// tests/fixtures/, where C checks A in a new entry, the ids and every
// parent and target resolve, A stays served, and E's forget of B leaves
// B's header for D's parent and B not served.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace fold = qiven3::entry::fold;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using fold::ItemView;
using fold::Method;
using fold::Result;
using fold::State;
using fold::Status;
using fold::Transcript;

constexpr std::array<std::string_view, 10> AT = {
    "2026-10-17T09:00:00.000Z", "2026-10-17T09:01:00.000Z", "2026-10-17T09:02:00.000Z", "2026-10-17T09:03:00.000Z",
    "2026-10-17T09:04:00.000Z", "2026-10-17T09:05:00.000Z", "2026-10-17T09:06:00.000Z", "2026-10-17T09:07:00.000Z",
    "2026-10-17T09:08:00.000Z", "2026-10-17T09:09:00.000Z"};
constexpr std::string_view NOW = "2026-10-18T00:00:00.000Z";
constexpr std::string_view OWN = "implementer/s39";
constexpr std::string_view OTHER = "reviewer/s70";

// An entry at minute m, in lane implementer with seq m.
constexpr ItemView e(std::string_view id, std::string_view kind, std::string_view target, std::size_t m,
                     std::string_view writer = OWN) {
    ItemView x;
    x.id = id;
    x.kind = kind;
    x.target = target;
    x.at = AT.at(m);
    x.lane = "implementer";
    x.writer = writer;
    x.seq = static_cast<std::int64_t>(m) + 1;
    return x;
}
constexpr ItemView check(std::string_view id, std::string_view target, std::size_t m, std::string_view writer,
                         Method method, Result result, Transcript t = Transcript::unverified) {
    ItemView x = e(id, "check", target, m, writer);
    x.method = method;
    x.result = result;
    x.transcript = t;
    return x;
}

template <std::size_t N>
constexpr State run(const std::array<ItemView, N>& r, std::string_view now = NOW) {
    return fold::fold(std::span<const ItemView>(r), now);
}
constexpr State state(Status s, std::uint32_t current, std::uint32_t forks = 0, std::uint32_t refuter = fold::none) {
    return State{s, current, forks, refuter};
}

constexpr ItemView V1 = e("V1", "claim", "", 0);
constexpr ItemView V2 = e("V2", "claim", "V1", 1);
constexpr ItemView V3 = e("V3", "claim", "V2", 2);

// The order: at first, then lane by code point, then seq as a number.
template <std::size_t N>
constexpr std::array<ItemView, N> sorted(std::array<ItemView, N> r) {
    fold::order(std::span<ItemView>(r));
    return r;
}
constexpr std::array<std::string_view, 5> ids(const std::array<ItemView, 5>& r) {
    return {r[0].id, r[1].id, r[2].id, r[3].id, r[4].id};
}
constexpr ItemView in(std::string_view id, std::string_view at, std::string_view lane, std::int64_t seq) {
    ItemView x = e(id, "claim", "", 0);
    x.at = at;
    x.lane = lane;
    x.seq = seq;
    return x;
}
static_assert(ids(sorted(std::array<ItemView, 5>{
                  in("s10", AT[1], "b", 10), in("s9", AT[1], "b", 9), in("late", AT[2], "a", 1),
                  in("lane-a", AT[1], "a", 99), in("first", AT[0], "z", 50)})) ==
              std::array<std::string_view, 5>{"first", "lane-a", "s9", "s10", "late"});
// Lanes by code point: "a-b" before "a0" before "ab".
static_assert(ids(sorted(std::array<ItemView, 5>{in("3", AT[0], "ab", 1), in("2", AT[0], "a0", 1),
                                                 in("1", AT[0], "a-b", 1), in("5", AT[3], "a", 1),
                                                 in("4", AT[0], "b", 1)})) ==
              std::array<std::string_view, 5>{"1", "2", "3", "4", "5"});

// One writer: equal, or one session n; loop and import only themselves.
static_assert(fold::one_writer(OWN, OWN) && fold::one_writer(OWN, "reflect/s39") &&
              fold::one_writer("reviewer/s39", OWN));
static_assert(!fold::one_writer(OWN, OTHER) && !fold::one_writer(OWN, "reviewer/s3") &&
              !fold::one_writer("implementer/s3", "implementer/s39"));
static_assert(fold::one_writer("loop", "loop") && !fold::one_writer("loop", "import") &&
              !fold::one_writer("loop", OWN));

// Steps 1 and 4: a root alone, a revision, a chain.
static_assert(run(std::array{V1}) == state(Status::served, 0));
static_assert(run(std::array{V1, V2}) == state(Status::served, 1));
static_assert(run(std::array{V1, V2, V3}) == state(Status::served, 2));
static_assert(run(std::array{V1}).active() && run(std::array{V1}).served());

// F51: retiring the current version serves the one it revised, not none.
static_assert(run(std::array{V1, V2, e("R", "retire", "V2", 3)}) == state(Status::served, 0));
// Retiring the root alone leaves its revision current.
static_assert(run(std::array{V1, V2, e("R", "retire", "V1", 3)}) == state(Status::served, 1));
// Every version withdrawn: retired, not served, not active.
constexpr State all_retired = run(std::array{V1, V2, e("R1", "retire", "V2", 3), e("R2", "retire", "V1", 4)});
static_assert(all_retired == state(Status::retired, fold::none) && !all_retired.active());
static_assert(!run(std::array{V1, e("R", "retire", "V1", 3)}).active());
// A retire counts by its header, its body forgotten.
constexpr ItemView unheld(ItemView x) {
    x.held = false;
    return x;
}
static_assert(run(std::array{V1, V2, unheld(e("R", "retire", "V2", 3))}) == state(Status::served, 0));

// F118: retiring only V2 of V1<-V2<-V3 serves V3 forked +1, V1 a leaf again.
static_assert(run(std::array{V1, V2, V3, e("R", "retire", "V2", 3)}) == state(Status::served, 2, 1));
// A fork: the last leaf in the order is current; retiring a leaf resolves it.
constexpr ItemView V3b = e("V3", "claim", "V1", 2);
static_assert(run(std::array{V1, V2, V3b}) == state(Status::served, 2, 1));
static_assert(run(std::array{V1, V2, V3b}).active());
static_assert(run(std::array{V1, V2, V3b, e("R", "retire", "V3", 3)}) == state(Status::served, 1));
static_assert(run(std::array{V1, V2, V3b, e("R", "retire", "V2", 3)}) == state(Status::served, 2));

// Step 5: the current version forgotten, by its body or by a forget: not
// served, no earlier version instead, not active.
static_assert(run(std::array{V1, unheld(V2)}) == state(Status::forgotten, 1));
static_assert(run(std::array{V1, V2, e("F", "forget", "V2", 3)}) == state(Status::forgotten, 1));
static_assert(!run(std::array{V1, unheld(V2)}).active());
// An earlier version forgotten leaves the current served.
static_assert(run(std::array{unheld(V1), V2, e("F", "forget", "V1", 3)}) == state(Status::served, 1));

// Step 6: expires at or before now; the record stays active.
constexpr ItemView expiring(ItemView x, std::string_view at) {
    x.expires = at;
    return x;
}
static_assert(run(std::array{expiring(V1, NOW)}) == state(Status::expired, 0));
static_assert(run(std::array{expiring(V1, "2026-10-17T23:59:59.999Z")}) == state(Status::expired, 0));
static_assert(run(std::array{expiring(V1, "2026-10-18T00:00:00.001Z")}) == state(Status::served, 0));
static_assert(run(std::array{expiring(V1, NOW)}).active());
// The revision's expires decides, not the root's.
static_assert(run(std::array{expiring(V1, AT[5]), V2}) == state(Status::served, 1));

// Step 7, F50 as F119 narrows it: a refutation by another writer, method
// test or rerun, or a transcript re-verified, ends the fold; active still.
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::rerun, Result::refuted)}) ==
              state(Status::refuted, 0, 0, 1));
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::test, Result::refuted)}) ==
              state(Status::refuted, 0, 0, 1));
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::transcript, Result::refuted,
                                       Transcript::reverified)}) == state(Status::refuted, 0, 0, 1));
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::rerun, Result::refuted)}).active());
// The first in the order is named.
static_assert(run(std::array{V1, check("C1", "V1", 1, OTHER, Method::test, Result::refuted),
                             check("C2", "V1", 2, "tester/s71", Method::rerun, Result::refuted)})
                  .refuter == 1);
// Served: a review's refutation (disputed, the labels' row), a transcript
// unverified or unequal, the own writer's or its session's, a verified
// result, an accept.
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::review, Result::refuted)}).served());
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::transcript, Result::refuted)}).served());
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::transcript, Result::refuted,
                                       Transcript::unequal)})
                  .served());
static_assert(run(std::array{V1, check("C", "V1", 1, OWN, Method::rerun, Result::refuted)}).served());
static_assert(run(std::array{V1, check("C", "V1", 1, "reflect/s39", Method::test, Result::refuted)}).served());
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::rerun, Result::verified)}).served());
static_assert(run(std::array{V1, e("ACC", "accept", "V1", 1, OTHER)}).served());
// Not live: retired, its body not held, or forgotten.
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::rerun, Result::refuted),
                             e("R", "retire", "C", 2, OTHER)})
                  .served());
static_assert(run(std::array{V1, unheld(check("C", "V1", 1, OTHER, Method::rerun, Result::refuted))}).served());
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::rerun, Result::refuted),
                             e("F", "forget", "C", 2, OTHER)})
                  .served());
// A check binds the version it targets: a revision starts unchecked.
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::rerun, Result::refuted), e("V2", "claim", "V1", 2)}) ==
              state(Status::served, 2));
// A refutation of the revision, the current version, ends the fold there.
static_assert(run(std::array{V1, V2, check("C", "V2", 2, OTHER, Method::test, Result::refuted)}) ==
              state(Status::refuted, 1, 0, 2));

// The first step that ends the fold decides: forgotten before expired,
// expired before refuted.
static_assert(run(std::array{unheld(expiring(V1, AT[0]))}).status == Status::forgotten);
static_assert(run(std::array{expiring(V1, AT[0]), check("C", "V1", 1, OTHER, Method::test, Result::refuted)})
                  .status == Status::expired);

// Entries that take no part: a target not among the earlier entries,
// another root, a revision of another kind, and a change of a change.
static_assert(run(std::array{V1, e("X", "claim", "V9", 1), e("W", "claim", "", 2), e("L", "lesson", "V1", 3)}) ==
              state(Status::served, 0));
static_assert(run(std::array{V1, e("X", "retire", "V9", 1)}) == state(Status::served, 0));
// A revision of a non-version (another root's) is no version.
static_assert(run(std::array{V1, e("W", "claim", "", 1), e("W2", "claim", "W", 2)}) == state(Status::served, 0));
// A check of a check binds no version.
static_assert(run(std::array{V1, check("C", "V1", 1, OTHER, Method::review, Result::verified),
                             check("CC", "C", 2, "tester/s71", Method::test, Result::refuted)})
                  .served());

// A stored line's entry, its content parsed where it has one.
struct Held {
    header::Header<> h;
    canon::Basic<> v;
};
Held held(const fixtures::EntryView& x) {
    canon::Basic<> content;
    if (x.content) {
        canon::parse(*x.content, content);
    }
    return Held{header::parse_line(x.header), std::move(content)};
}

std::optional<fold::Code> fault(std::span<const ItemView> r, std::string_view now = NOW) {
    try {
        static_cast<void>(fold::fold(r, now));
    } catch (const fold::FoldError& f) {
        return f.code;
    }
    return std::nullopt;
}

}  // namespace

QIVEN3_CASE(fold, refusals) {
    const std::array<ItemView, 2> ok = {V1, V2};
    CHECK(!fault(ok).has_value());
    // No root: none at all, a change first, a first entry with a target.
    CHECK(fault(std::span<const ItemView>()) == fold::Code::root);
    CHECK(fault(std::array{e("C", "check", "V1", 0)}) == fold::Code::root);
    CHECK(fault(std::array{V2}) == fold::Code::root);
    // Out of order, or one key twice.
    try {
        static_cast<void>(fold::fold(std::array{V1, V3, V2}, NOW));
        CHECK(false);
    } catch (const fold::FoldError& f) {
        CHECK(f.code == fold::Code::order);
        CHECK_EQ(f.index, 2U);
    }
    CHECK(fault(std::array{V1, V1}) == fold::Code::order);
    // now not in at's syntax.
    for (const std::string_view now : {"", "2026-10-18", "2026-10-18T00:00:00Z", "2026-02-30T00:00:00.000Z"}) {
        CHECK(fault(ok, now) == fold::Code::now);
    }
    CHECK_EQ(std::string(fold::name(fold::Code::order)), "order");
    CHECK_EQ(std::string(fold::name(fold::Code::now)), "now");
}

QIVEN3_CASE(fold, fa2_status_change) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<std::string_view> names = {"A", "B", "C", "D", "ACC", "E", "M1", "M2", "ACC2"};
    std::vector<Held> all;
    for (const std::string_view name : names) {
        const std::optional<fixtures::EntryView> x = fixtures::find_entry(v, name);
        REQUIRE(x.has_value());
        all.push_back(held(*x));
    }
    const auto at = [&](std::string_view name) -> const Held& {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return all[i];
            }
        }
        return all.front();
    };
    // Each id recomputed from its header; every parent and target resolves.
    std::size_t resolved = 0;
    for (const Held& x : all) {
        CHECK_EQ(std::string(header::id_of(x.h).view()), std::string(x.h.value(header::Slot::id)));
        for (const header::Slot s : {header::Slot::parent, header::Slot::target}) {
            if (!x.h.present(s)) {
                continue;
            }
            bool found = false;
            for (const Held& y : all) {
                found = found || y.h.value(header::Slot::id) == x.h.value(s);
            }
            CHECK(found);
            resolved += found ? 1U : 0U;
        }
    }
    const std::string a_id(at("A").h.value(header::Slot::id));
    CHECK_EQ(a_id.substr(0, 15), "sha256:90238039");
    CHECK_EQ(std::string(at("B").h.value(header::Slot::parent)), a_id);
    const auto item = [&](std::string_view name, bool body) { return fold::item(at(name).h, at(name).v, body); };
    // A alone, then C, a check of A by reviewer/s70, a new entry: A served.
    const std::array<ItemView, 1> a = {item("A", true)};
    CHECK(fold::fold(a, NOW) == state(Status::served, 0));
    std::array<ItemView, 2> ac = {item("C", true), item("A", true)};
    fold::order(ac);
    CHECK_EQ(std::string(ac[0].id), a_id);
    CHECK(fold::fold(ac, NOW) == state(Status::served, 0));
    CHECK(ac[1].method == Method::rerun && ac[1].result == Result::verified);
    CHECK_EQ(std::string(ac[1].target), a_id);
    CHECK_EQ(std::string(ac[1].writer), "reviewer/s70");
    // E forgets B: B's header stays, D's parent resolves to it, B not served.
    const std::string b_id(at("B").h.value(header::Slot::id));
    CHECK_EQ(std::string(at("D").h.value(header::Slot::parent)), b_id);
    CHECK_EQ(std::string(at("E").h.value(header::Slot::target)), b_id);
    const std::array<ItemView, 1> b = {item("B", true)};
    CHECK(fold::fold(b, NOW) == state(Status::served, 0));
    const std::array<ItemView, 2> be = {item("B", false), item("E", false)};
    const State forgotten = fold::fold(be, NOW);
    CHECK(forgotten == state(Status::forgotten, 0));
    CHECK(!forgotten.active() && !forgotten.served());
    // D with its own writer's ACC and reviewer/s70's ACC2: served; M2
    // revises M1.
    const std::array<ItemView, 3> d = {item("D", true), item("ACC", true), item("ACC2", true)};
    CHECK(fold::fold(d, NOW) == state(Status::served, 0));
    const std::array<ItemView, 2> m = {item("M1", true), item("M2", true)};
    CHECK(fold::fold(m, NOW) == state(Status::served, 1));
    std::cout << "fold.fa2_status_change: " << all.size() << " ids recomputed, " << resolved
              << " parents and targets resolved\n";
    CHECK_EQ(all.size(), 9U);
}

QIVEN3_CASE(fold, scope_order) {
    // The fixtures' entries of project:qiven in the order: each parent and
    // each target before its child.
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<Held> all;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        Held x = held(fixtures::entry(v, i));
        if (x.h.value(header::Slot::scope) == "project:qiven") {
            all.push_back(std::move(x));
        }
    }
    std::vector<ItemView> items;
    for (const Held& x : all) {
        items.push_back(fold::item(x.h, x.v, true));
    }
    fold::order(items);
    std::string line;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            CHECK(fold::detail::before(items[i - 1], items[i]));
        }
        for (const Held& x : all) {
            if (x.h.value(header::Slot::id) != items[i].id) {
                continue;
            }
            for (const header::Slot s : {header::Slot::parent, header::Slot::target}) {
                bool earlier = !x.h.present(s);
                for (std::size_t j = 0; j < i; ++j) {
                    earlier = earlier || items[j].id == x.h.value(s);
                }
                CHECK(earlier);
            }
        }
        line += std::string(items[i].lane) + "/" + std::to_string(items[i].seq) + " ";
    }
    std::cout << "fold.scope_order: " << items.size() << " entries: " << line << "\n";
    CHECK(items.size() >= 9U);
}
