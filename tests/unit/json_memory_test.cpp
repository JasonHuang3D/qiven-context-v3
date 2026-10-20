// json's budgets and fault runs: scan and encode within the specification's 64 KiB
// plus 4 times the input's bytes, counted by the arena, a byte short a
// MemoryError budget with the caller's value or string unchanged; and each
// allocation of each failed in turn, a typed error with nothing held.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/json.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
namespace json = qiven3::json;

std::string repeat(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

// The worst case for the tape, an array of one-digit integers, two bytes a
// node; and a mixed document, objects, strings, escapes and arrays.
std::string worst() { return "[" + repeat("1,", 20000) + "1]"; }
std::string mixed() {
    return "[" + repeat(R"({"id":12345,"name":"caf\u00e9 \ud834\udd1e","tags":["a","bb",true],"n":{"k":-1}},)", 300) +
           "{}]";
}

constexpr std::size_t budget_for(std::size_t input) { return 65536 + 4 * input; }

bool budget_error(const MemoryError& e) { return e.code == MemoryCode::budget; }

}  // namespace

QIVEN3_CASE(json, scan_budget) {
    for (const std::string& text : {worst(), mixed()}) {
        const std::size_t budget = budget_for(text.size());
        Arena& arena = qiven3_ctx.counted.arena(budget);
        std::size_t peak = 0;
        std::size_t nodes = 0;
        {
            json::Value v{Allocator<char>(arena)};
            json::scan(text, 16, v);
            nodes = v.size();
            peak = arena.peak();
        }
        std::cout << "json.scan_budget: input " << text.size() << " bytes, " << nodes << " nodes, peak " << peak
                  << ", budget " << budget << "\n";
        CHECK(peak <= budget);
        CHECK(peak >= 8 * nodes);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        // A byte short: MemoryError budget, the caller's value unchanged.
        // MSVC's debug containers allocate in their noexcept constructors,
        // where scan's keys meet the short budget and the process ends, so
        // this half runs where its iterators do not debug: asan, CI.
        Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
        json::Value v{Allocator<char>(shorter)};
        bool refused = false;
        try {
            json::scan(text, 16, v);
        } catch (const MemoryError& e) {
            refused = budget_error(e);
        }
        CHECK(refused);
        CHECK(v.empty());
#else
        std::cout << "json.scan_budget: a byte short not run, under MSVC's debug containers\n";
#endif
    }
}

QIVEN3_CASE(json, encode_budget) {
    for (const std::string& text : {worst(), mixed()}) {
        json::Basic<> value;
        json::scan(text, 16, value);
        std::string want;
        json::encode(value, want);
        const std::size_t budget = budget_for(want.size());
        Arena& arena = qiven3_ctx.counted.arena(budget);
        std::size_t peak = 0;
        {
            String out{Allocator<char>(arena)};
            json::encode(value, out);
            CHECK(std::string_view(out) == want);
            peak = arena.peak();
        }
        std::cout << "json.encode_budget: output " << want.size() << " bytes, peak " << peak << ", budget " << budget
                  << "\n";
        CHECK(peak <= budget);
        Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
        String out{Allocator<char>(shorter)};
        bool refused = false;
        try {
            json::encode(value, out);
        } catch (const MemoryError& e) {
            refused = budget_error(e);
        }
        CHECK(refused);
        CHECK(out.empty());
    }
}

QIVEN3_CASE(json, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::string text = mixed();
    std::vector<std::byte> pages(budget_for(text.size()));
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    std::size_t nodes = 0;
    const auto scan = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        json::Value v{Allocator<char>(*arena)};
        json::scan(text, 16, v);
        nodes = v.size();
    };
    const qiven3::test::FaultRuns scans = qiven3::test::run_faults<MemoryError>(countdown, scan, check);
    std::cout << "json.faults: scan's allocations " << scans.typed << ", completed at " << scans.completed_at << "\n";
    // The nodes, the text and the keys' check, one allocation each.
    CHECK_EQ(scans.typed, 3U);
    CHECK_EQ(scans.completed_at, 4U);
    CHECK_EQ(scans.untyped, 0U);
    CHECK_EQ(scans.unchecked, 0U);
    CHECK(nodes > 0);

    json::Basic<> value;
    json::scan(text, 16, value);
    std::size_t bytes = 0;
    const auto encode = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        String out{Allocator<char>(*arena)};
        json::encode(value, out);
        bytes = out.size();
    };
    const qiven3::test::FaultRuns encodes = qiven3::test::run_faults<MemoryError>(countdown, encode, check);
    std::cout << "json.faults: encode's allocations " << encodes.typed << ", completed at " << encodes.completed_at
              << "\n";
    // The sorted members and the bytes.
    CHECK_EQ(encodes.typed, 2U);
    CHECK_EQ(encodes.completed_at, 3U);
    CHECK_EQ(encodes.untyped, 0U);
    CHECK_EQ(encodes.unchecked, 0U);
    CHECK(bytes > 0);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
