// secret's budgets and fault runs: check compiles the twelve patterns, two
// allocations each, holding the automata until it returns, then takes the
// hits' room, none for no hit, one for the first four, one more past four;
// over an arena it leaves the arena's use where it was but the hits it
// returns, and its cursor moved by at most each pattern's scratch and
// automaton, the hits' rooms and their alignment. Each allocation, failed
// in turn, is a MemoryError with nothing held once the check ends; the run
// completes past the last, which counts them exactly: no hit 24, four hits
// 25, five hits 26, a header hit 24, refused. The runner's count after each
// case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "entry/matcher.hpp"
#include "entry/secret.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace secret = qiven3::entry::secret;
namespace header = qiven3::entry::header;
namespace matcher = qiven3::entry::matcher;
namespace canon = qiven3::canon;
namespace json = qiven3::json;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

// A case: its lane, its content's strings under "tags", the hits and the
// allocations its check takes.
struct Case {
    std::string name;
    std::string lane;
    std::vector<std::string> tags;
    std::size_t hits;
    std::size_t allocations;
};

std::string rep(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

std::vector<Case> cases() {
    const std::string hit = "--token " + rep("t", 20);
    const std::string miss = "a plain tag";
    return {
        Case{"no hit", "implementer", {miss, miss}, 0, 24},
        Case{"four hits", "implementer", {hit, miss, hit, hit, hit}, 4, 25},
        Case{"five hits", "implementer", {hit, hit, hit, hit, miss, hit}, 5, 26},
        Case{"a header hit", "xoxb-1-" + rep("a", 8), {hit}, 0, 24},
    };
}

header::Header<> make(const Case& c) {
    header::FieldsView f;
    f.scope = "project:qiven";
    f.lane = c.lane;
    f.kind = "claim";
    f.writer = "implementer";
    f.basis = "observed";
    f.at = "2026-10-17T09:30:00.000Z";
    f.id = "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    return header::make(f);
}

void fill(const Case& c, canon::Basic<Allocator<char>>& v) {
    json::Writer<Allocator<char>> w(v);
    w.open(json::Kind::object);
    w.string("tags");
    w.open(json::Kind::array);
    for (const std::string& t : c.tags) {
        w.string(t);
    }
    w.close();
    w.close();
}

// The hits' rooms taken for n hits, 4, then 8, ...: the last, or their sum.
std::size_t rooms(std::size_t n, bool last) {
    std::size_t room = 0;
    std::size_t sum = 0;
    while (room < n) {
        room = room == 0 ? secret::first_hits : room * 2;
        sum += room;
    }
    return last ? room : sum;
}

// The twelve compiles' bytes: each a scratch and its automaton.
std::size_t compiles() {
    std::size_t sum = 0;
    for (const secret::Pattern& p : secret::PATTERNS) {
        const matcher::Basic<> a = matcher::compile(p.text.view());
        sum += sizeof(matcher::detail::Scratch) + matcher::automaton_bytes(a.positions());
    }
    return sum;
}

constexpr std::size_t budget = (secret::PATTERNS.size() + 2) * matcher::compile_budget;

}  // namespace

QIVEN3_CASE(secret, budget) {
    for (const Case& c : cases()) {
        const header::Header<> h = make(c);
        Arena& arena = qiven3_ctx.counted.arena(budget);
        canon::Basic<Allocator<char>> v{Allocator<char>(arena)};
        fill(c, v);
        const std::size_t used = arena.used();
        const std::size_t before = arena.peak();
        std::optional<std::size_t> got;
        bool refused = false;
        {
            secret::Hits<Allocator<char>> out{Allocator<secret::Hit>(arena)};
            refused = secret::try_check(h, v, out).has_value();
            got = out.size();
            const std::size_t held = out.capacity() * sizeof(secret::Hit);
            CHECK_EQ(out.capacity(), rooms(c.hits, true));
            CHECK(arena.used() >= used + held);
        }
        CHECK_EQ(arena.used(), used);
        CHECK_EQ(refused, c.lane != "implementer");
        CHECK_EQ(*got, c.hits);
        const std::size_t moved = arena.peak() - before;
        const std::size_t bound = compiles() + rooms(c.hits, false) * sizeof(secret::Hit) + 16 * c.allocations;
        std::cout << "secret.budget: " << c.name << ": cursor moved " << moved << " bytes, bound " << bound << "\n";
        CHECK(moved <= bound);
    }
}

QIVEN3_CASE(secret, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    bool done = false;
    // The value, the automata and the hits given back once the check ends.
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    for (const Case& c : cases()) {
        const header::Header<> h = make(c);
        const auto op = [&] {
            arena.emplace(pages);
            canon::Basic<Allocator<char>> v{Allocator<char>(*arena)};
            fill(c, v);
            arena->attach(&countdown);
            try {
                done = secret::check(h, v).size() == c.hits;
            } catch (const secret::SecretError& e) {
                done = e.path.view() == "header.lane";
            }
            arena->attach(nullptr);
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "secret.check", countdown, op, empty);
        std::cout << "secret.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, c.allocations);
        CHECK_EQ(runs.completed_at, c.allocations + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        done = false;
    }
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
