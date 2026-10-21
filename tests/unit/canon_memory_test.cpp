// canon's budgets and fault runs: parse and canonical within the
// specification's 64 KiB plus 4 times the input's bytes, counted by the
// arena; a byte short a MemoryError budget with the caller's value or
// string unchanged; and each allocation of each failed in turn, a typed
// error with nothing held. The runner's count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
namespace canon = qiven3::canon;

std::string repeat(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

// Canonical inputs: the tape's worst case, an array of one-digit integers,
// two bytes a node; and a mixed document, objects, strings in English and
// Chinese, escapes and arrays.
std::string worst() { return "[" + repeat("1,", 20000) + "1]"; }
std::string mixed() {
    return "[" +
           repeat("{\"id\":12345,\"n\":{\"k\":-1},\"name\":\"caf\xc3\xa9 \xe8\xae\xb0\xe5\xbf\x86 \\u0001\\\"\","
                  "\"tags\":[\"a\",\"bb\",true]},",
                  300) +
           "{}]";
}

constexpr std::size_t budget_for(std::size_t input) { return 65536 + 4 * input; }

bool budget_error(const MemoryError& e) { return e.code == MemoryCode::budget; }

}  // namespace

QIVEN3_CASE(canon, parse_budget) {
    for (const std::string& text : {worst(), mixed()}) {
        const std::size_t budget = budget_for(text.size());
        Arena& arena = qiven3_ctx.counted.arena(budget);
        std::size_t peak = 0;
        std::size_t nodes = 0;
        {
            canon::Value v{Allocator<char>(arena)};
            canon::parse(text, v);
            nodes = v.size();
            peak = arena.peak();
        }
        std::cout << "canon.parse_budget: input " << text.size() << " bytes, " << nodes << " nodes, peak " << peak
                  << ", budget " << budget << "\n";
        CHECK(peak <= budget);
        CHECK(arena.used() == 0);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        // A byte short: MemoryError budget, the caller's value unchanged.
        // MSVC's debug containers allocate in their noexcept constructors,
        // where the short budget ends the process, so this half runs where
        // its iterators do not debug: asan, CI.
        Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
        canon::Value v{Allocator<char>(shorter)};
        bool refused = false;
        try {
            canon::parse(text, v);
        } catch (const MemoryError& e) {
            refused = budget_error(e);
        }
        CHECK(refused);
        CHECK(v.empty());
#else
        std::cout << "canon.parse_budget: a byte short not run, under MSVC's debug containers\n";
#endif
    }
}

QIVEN3_CASE(canon, canonical_budget) {
    for (const std::string& text : {worst(), mixed()}) {
        canon::Basic<> value;
        canon::parse(text, value);
        const std::size_t budget = budget_for(text.size());
        Arena& arena = qiven3_ctx.counted.arena(budget);
        std::size_t peak = 0;
        {
            String out{Allocator<char>(arena)};
            canon::canonical(value, out);
            CHECK(std::string_view(out) == text);
            peak = arena.peak();
        }
        std::cout << "canon.canonical_budget: output " << text.size() << " bytes, peak " << peak << ", budget "
                  << budget << "\n";
        CHECK(peak <= budget);
        Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
        String out{Allocator<char>(shorter)};
        bool refused = false;
        try {
            canon::canonical(value, out);
        } catch (const MemoryError& e) {
            refused = budget_error(e);
        }
        CHECK(refused);
        CHECK(out.empty());
    }
}

QIVEN3_CASE(canon, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::string text = mixed();
    std::vector<std::byte> pages(budget_for(text.size()));
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    std::size_t nodes = 0;
    const auto parse = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        canon::Value v{Allocator<char>(*arena)};
        canon::parse(text, v);
        nodes = v.size();
    };
    const qiven3::test::FaultRuns parses =
        qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "canon.parse", countdown, parse, check);
    std::cout << "canon.faults: parse's allocations " << parses.typed << ", completed at " << parses.completed_at
              << "\n";
    // The scan's nodes, text and keys' check, then the comparison's sorted
    // members.
    CHECK_EQ(parses.typed, 4U);
    CHECK_EQ(parses.completed_at, 5U);
    CHECK_EQ(parses.untyped, 0U);
    CHECK_EQ(parses.unchecked, 0U);
    CHECK(nodes > 0);

    canon::Basic<> value;
    canon::parse(text, value);
    std::size_t bytes = 0;
    const auto write = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        String out{Allocator<char>(*arena)};
        canon::canonical(value, out);
        bytes = out.size();
    };
    const qiven3::test::FaultRuns writes =
        qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "canon.canonical", countdown, write, check);
    std::cout << "canon.faults: canonical's allocations " << writes.typed << ", completed at "
              << writes.completed_at << "\n";
    // The sorted members and the bytes.
    CHECK_EQ(writes.typed, 2U);
    CHECK_EQ(writes.completed_at, 3U);
    CHECK_EQ(writes.untyped, 0U);
    CHECK_EQ(writes.unchecked, 0U);
    CHECK_EQ(bytes, text.size());
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
