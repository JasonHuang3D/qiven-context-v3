// digest's budget and fault run: plain within the specification's 4 KiB
// beside the input, counted by the arena, one allocation, its result's; a
// byte short a MemoryError budget with the caller's string unchanged; each
// allocation failed in turn, a typed error with nothing held. The runner's
// count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven/digest/digest.hpp"
#include "qiven/faults/faults.hpp"
#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace digest = qiven::digest;
using qiven::memory::Allocator;
using qiven::memory::Arena;
using qiven::memory::MemoryCode;
using qiven::memory::MemoryError;
using qiven::memory::String;

constexpr std::size_t budget_for(std::size_t input) { return 4096 + input; }

// The case's data: 1,196 bytes, in English and Chinese (摘要).
std::string data() { return "digest's own data, \xe6\x91\x98\xe8\xa6\x81: " + std::string(1169, 'x'); }

}  // namespace

QIVEN_CASE(digest, budgets) {
    const std::string input = data();
    CHECK_EQ(input.size(), 1196U);
    const std::size_t budget = budget_for(input.size());
    Arena& arena = qiven_ctx.counted.arena(budget);
    std::size_t peak = 0;
    std::size_t allocations = 0;
    {
        String out{Allocator<char>(arena)};
        digest::plain(input, out);
        peak = arena.peak();
        allocations = out.empty() ? 0U : 1U;
    }
    std::cout << "digest.plain: input " << input.size() << " bytes, peak " << peak << ", budget " << budget
              << ", result allocations " << allocations << "\n";
    CHECK(peak <= budget);
    CHECK(arena.used() == 0);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    Arena& shorter = qiven_ctx.counted.arena(peak - 1);
    String out{Allocator<char>(shorter)};
    bool refused = false;
    try {
        digest::plain(input, out);
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget;
    }
    CHECK(refused);
    CHECK(out.empty());
#else
    std::cout << "digest.plain: a byte short not run, under MSVC's debug containers\n";
#endif
}

QIVEN_CASE(digest, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::string input = data();
    std::vector<std::byte> pages(budget_for(input.size()));
    qiven::memory::Countdown countdown;
    std::optional<Arena> arena;
    const qiven::test::FaultRuns r = qiven::test::run_faults<MemoryError>(
        qiven_ctx.log, "digest.plain", countdown,
        [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            String out{Allocator<char>(*arena)};
            digest::plain(input, out);
        },
        [&](std::size_t) { return arena->used() == 0; });
    arena.reset();
    std::cout << "digest.faults: plain's allocations " << r.typed << ", completed at " << r.completed_at << "\n";
    // The result's one allocation.
    CHECK_EQ(r.typed, 1U);
    CHECK_EQ(r.completed_at, 2U);
    CHECK_EQ(r.untyped, 0U);
    CHECK_EQ(r.unchecked, 0U);
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
