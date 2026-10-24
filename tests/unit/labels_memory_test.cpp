// labels' budgets and fault run: label, version_of and check_of take no
// allocation. Over every fixture entry's header and content, held in an
// arena, each check or accept read and the label of each version with all
// of them leave the arena's use and peak where they were; with the arena's
// countdown armed at its first allocation, they complete, no call failed.
// The runner's count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "entry/header.hpp"
#include "entry/labels.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace header = qiven3::entry::header;
namespace labels = qiven3::entry::labels;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

constexpr std::size_t budget = 1048576;

struct Stored {
    std::string header;
    std::string content;
};

std::vector<Stored> stored() {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<Stored> out;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        out.push_back(Stored{std::string(e.header), std::string(e.content.value_or(""))});
    }
    return out;
}

struct Parsed {
    header::Header<Allocator<char>> h;
    canon::Basic<Allocator<char>> v;
};

std::vector<Parsed> parsed(const std::vector<Stored>& all, Arena& arena) {
    std::vector<Parsed> out;
    for (const Stored& s : all) {
        canon::Basic<Allocator<char>> content{Allocator<char>(arena)};
        if (!s.content.empty()) {
            canon::parse(s.content, content);
        }
        out.push_back(Parsed{header::parse_line(s.header, Allocator<char>(arena)), std::move(content)});
    }
    return out;
}

// Each check and accept read, then each version's label with all of them:
// the labels made.
std::size_t label_all(const std::vector<Parsed>& all, std::vector<labels::CheckView>& checks) {
    checks.clear();
    for (const Parsed& p : all) {
        const std::optional<labels::CheckView> c = labels::check_of(p.h, p.v, true);
        if (c) {
            checks.push_back(*c);
        }
    }
    std::size_t made = 0;
    for (const Parsed& p : all) {
        const std::optional<labels::VersionView> v = labels::version_of(p.h);
        if (v) {
            made += labels::label(*v, checks).text().empty() ? 0U : 1U;
        }
    }
    return made;
}

}  // namespace

QIVEN3_CASE(labels, budget) {
    const std::vector<Stored> all = stored();
    Arena& arena = qiven3_ctx.counted.arena(budget);
    const std::vector<Parsed> entries = parsed(all, arena);
    std::vector<labels::CheckView> checks;
    checks.reserve(entries.size());
    const std::size_t used = arena.used();
    const std::size_t peak = arena.peak();
    const std::size_t made = label_all(entries, checks);
    CHECK_EQ(arena.used(), used);
    CHECK_EQ(arena.peak(), peak);
    std::cout << "labels.budget: " << made << " labels over " << checks.size() << " checks and accepts, 0 allocations\n";
    CHECK_EQ(made, all.size());
    CHECK_EQ(checks.size(), 3U);
}

QIVEN3_CASE(labels, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::vector<Stored> all = stored();
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    std::vector<labels::CheckView> checks;
    checks.reserve(all.size());
    std::size_t made = 0;
    const auto op = [&] {
        arena.emplace(pages);
        const std::vector<Parsed> entries = parsed(all, *arena);
        arena->attach(&countdown);
        made = label_all(entries, checks);
        arena->attach(nullptr);
    };
    const auto empty = [&](std::size_t) { return true; };
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "labels.label", countdown, op, empty);
    std::cout << "labels.faults: label's allocations " << runs.typed << ", completed at " << runs.completed_at << "\n";
    CHECK_EQ(runs.typed, 0U);
    CHECK_EQ(runs.completed_at, 1U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(made, all.size());
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
