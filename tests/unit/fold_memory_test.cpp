// fold's budgets and fault run: fold takes one allocation, its work of 8
// bytes an entry, freed before it returns; order, item and one_writer none.
// Over an arena, fold of 1, 2 and 64 entries raises the peak by its work
// alone and leaves the use where it was; an arena a byte short of the work
// is MemoryError, nothing held. With the arena's countdown armed, the fault
// run counts one allocation and completes at the second. The runner's
// count after each case holds too.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace fold = qiven3::entry::fold;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using fold::ItemView;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

constexpr std::string_view NOW = "2026-10-18T00:00:00.000Z";

// A chain of n versions, each revising the one before, at one minute
// apart; ids and times held in the caller's strings.
struct Chain {
    std::vector<std::string> ids;
    std::vector<std::string> ats;
    std::vector<ItemView> items;
};
Chain chain(std::size_t n) {
    Chain c;
    for (std::size_t i = 0; i < n; ++i) {
        c.ids.push_back("V" + std::to_string(i));
        const std::size_t h = i / 60;
        const std::size_t m = i % 60;
        c.ats.push_back("2026-10-17T" + std::string(h < 10 ? "0" : "") + std::to_string(h) + ":" +
                        std::string(m < 10 ? "0" : "") + std::to_string(m) + ":00.000Z");
    }
    for (std::size_t i = 0; i < n; ++i) {
        ItemView x;
        x.id = c.ids[i];
        x.kind = "claim";
        x.target = i == 0 ? std::string_view{} : std::string_view(c.ids[i - 1]);
        x.at = c.ats[i];
        x.lane = "implementer";
        x.writer = "implementer/s39";
        x.seq = static_cast<std::int64_t>(i) + 1;
        c.items.push_back(x);
    }
    return c;
}

}  // namespace

QIVEN_CASE(fold, budget) {
    for (const std::size_t n : {std::size_t{1}, std::size_t{2}, std::size_t{64}}) {
        const Chain c = chain(n);
        Arena& arena = qiven_ctx.counted.arena(64 + 8 * n);
        const std::size_t before = arena.peak();
        const fold::State s = fold::fold(std::span<const ItemView>(c.items), NOW, Allocator<char>(arena));
        CHECK(s.served() && s.current == n - 1);
        CHECK_EQ(arena.used(), 0U);
        CHECK_EQ(arena.peak() - before, 8 * n);
        std::cout << "fold.budget: " << n << " entries, " << arena.peak() - before << " bytes\n";
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        bool refused = false;
        try {
            Arena& shorter = qiven_ctx.counted.arena(arena.peak() - 1);
            static_cast<void>(fold::fold(std::span<const ItemView>(c.items), NOW, Allocator<char>(shorter)));
        } catch (const MemoryError&) {
            refused = true;
        }
        CHECK(refused);
#endif
    }
    // order, item and one_writer allocate nothing.
    canon::Basic<> v;
    fixtures::load_entries(v);
    const std::optional<fixtures::EntryView> a = fixtures::find_entry(v, "A");
    REQUIRE(a.has_value() && a->content.has_value());
    Arena& none = qiven_ctx.counted.arena(65536);
    const header::Header<Allocator<char>> h = header::parse_line(a->header, Allocator<char>(none));
    canon::Basic<Allocator<char>> content{Allocator<char>(none)};
    canon::parse(*a->content, content);
    const std::size_t used = none.used();
    const std::size_t peak = none.peak();
    std::array<ItemView, 2> two = {chain(2).items[1], fold::item(h, content, true)};
    two[0].at = "2026-10-17T09:31:00.000Z";
    fold::order(two);
    CHECK(fold::one_writer("implementer/s39", "reflect/s39"));
    CHECK_EQ(none.used(), used);
    CHECK_EQ(none.peak(), peak);
}

QIVEN_CASE(fold, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const Chain c = chain(3);
    std::vector<std::byte> pages(1024);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    std::optional<fold::State> s;
    const auto op = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        s = fold::fold(std::span<const ItemView>(c.items), NOW, Allocator<char>(*arena));
        arena->attach(nullptr);
    };
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<MemoryError>(qiven_ctx.log, "fold.fold", countdown, op, empty);
    std::cout << "fold.faults: fold's allocations " << runs.typed << ", completed at " << runs.completed_at << "\n";
    CHECK_EQ(runs.typed, 1U);
    CHECK_EQ(runs.completed_at, 2U);
    CHECK_EQ(runs.untyped, 0U);
    REQUIRE(s.has_value());
    CHECK(s->served() && s->current == 2U);
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
