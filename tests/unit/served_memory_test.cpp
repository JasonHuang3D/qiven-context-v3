// served's budgets and fault runs: block and stub each take one
// allocation, the text's, written into the caller's arena string; cost,
// block_cost and map_mark none. Over an arena, M1's, L2's and MX's block
// and MX's stub raise the use by their text alone, with no peak past it;
// an arena a byte short of it is MemoryError, the caller's string as it
// was. With the arena's countdown armed, each fault run counts one
// allocation and completes at the second. The runner's count after each
// case holds too.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "entry/labels.hpp"
#include "entry/served.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace served = qiven3::entry::served;
namespace fold = qiven3::entry::fold;
namespace header = qiven3::entry::header;
namespace labels = qiven3::entry::labels;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using qiven3::memory::String;

constexpr served::State SERVED{fold::State{fold::Status::served, 0, 0, fold::none}, 0};

struct Held {
    header::Header<> h;
    canon::Basic<> v;
    labels::Label label;
    std::optional<served::MapMark> mark;
};
Held held(const canon::Basic<>& all, std::string_view name) {
    const std::optional<fixtures::EntryView> x = fixtures::find_entry(all, name);
    if (!x || !x->content) {
        throw std::runtime_error("no fixture entry");
    }
    Held e{header::parse_line(x->header), canon::Basic<>(), labels::Label(), std::nullopt};
    canon::parse(*x->content, e.v);
    e.label = labels::label(*labels::version_of(e.h, e.v), {});
    if (name == "M1") {
        e.mark = served::map_mark(served::Mark::stale, "2026-10-17T12:00:00.000Z");
    }
    return e;
}

// One operation into out: the block, or the stub.
void write(const Held& x, bool stub, String& out) {
    if (stub) {
        served::stub(x.h, x.v, SERVED, x.label, x.mark, out);
    } else {
        served::block(x.h, x.v, SERVED, x.label, x.mark, out);
    }
}

}  // namespace

QIVEN3_CASE(served, budget) {
    canon::Basic<> all;
    fixtures::load_entries(all);
    for (const auto& [name, stub] : {std::pair<std::string_view, bool>{"M1", false}, {"L2", false}, {"MX", false},
                                     {"MX", true}}) {
        const Held x = held(all, name);
        Arena& arena = qiven3_ctx.counted.arena(1U << 20U);
        String out{Allocator<char>(arena)};
        const std::size_t before = arena.peak();
        write(x, stub, out);
        const std::size_t used = arena.used();
        CHECK_EQ(arena.peak() - before, used);
        CHECK(used > out.size());
        // cost and block_cost take none.
        CHECK_EQ(served::cost(out), stub ? served::cost(out)
                                         : served::block_cost(x.h, x.v, SERVED, x.label, x.mark));
        CHECK_EQ(arena.used(), used);
        CHECK_EQ(arena.peak() - before, used);
        std::cout << "served.budget: " << name << (stub ? " stub " : " block ") << out.size() << " bytes, "
                  << used << " held\n";
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        bool refused = false;
        Arena& shorter = qiven3_ctx.counted.arena(used - 1);
        String kept{Allocator<char>(shorter)};
        kept = "x";  // within the string's own buffer: no allocation
        try {
            write(x, stub, kept);
        } catch (const MemoryError&) {
            refused = true;
        }
        CHECK(refused);
        CHECK(kept == "x" && shorter.used() == 0);
#endif
    }
}

QIVEN3_CASE(served, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    canon::Basic<> all;
    fixtures::load_entries(all);
    const Held m1 = held(all, "M1");
    std::vector<std::byte> pages(4096);
    for (const bool stub : {false, true}) {
        qiven3::memory::Countdown countdown;
        std::optional<Arena> arena;
        std::size_t size = 0;
        const auto op = [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            String out{Allocator<char>(*arena)};
            write(m1, stub, out);
            size = out.size();
            arena->attach(nullptr);
        };
        const auto empty = [&](std::size_t) { return arena->used() == 0; };
        const qiven3::test::FaultRuns runs =
            stub ? qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "served.stub", countdown, op, empty)
                 : qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "served.block", countdown, op, empty);
        std::cout << "served.faults: " << (stub ? "stub" : "block") << "'s allocations " << runs.typed
                  << ", completed at " << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, 1U);
        CHECK_EQ(runs.completed_at, 2U);
        CHECK_EQ(runs.untyped, 0U);
        CHECK(size > 0);
        arena.reset();
    }
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
