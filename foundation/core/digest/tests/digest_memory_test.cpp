// digest's budgets and fault runs: plain, and keyed over a 32-byte key's
// bytes with the case's wipe, within the specification's 4 KiB beside the
// input, counted by the arena, each one allocation, its result's; a byte
// short a MemoryError budget with the caller's string unchanged; each
// allocation failed in turn, a typed error with nothing held. The runner's
// count after each case holds too.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
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

// The case's key: 32 bytes counting up from 0.
std::array<std::byte, digest::bytes> counting_key() {
    std::array<std::byte, digest::bytes> key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(i);
    }
    return key;
}

// The case's wipe: the bytes it is given, zeroed.
void zero(std::span<std::byte> bytes) noexcept {
    for (std::byte& b : bytes) {
        b = std::byte{0};
    }
}

// One operation of digest's, writing into out.
template <typename Op>
void budget_case(qiven::runner::Context& qiven_ctx, std::string_view name, std::size_t input, const Op& op) {
    const std::size_t budget = budget_for(input);
    Arena& arena = qiven_ctx.counted.arena(budget);
    std::size_t peak = 0;
    std::size_t allocations = 0;
    {
        String out{Allocator<char>(arena)};
        op(out);
        peak = arena.peak();
        allocations = out.empty() ? 0U : 1U;
    }
    std::cout << "digest." << name << ": input " << input << " bytes, peak " << peak << ", budget " << budget
              << ", result allocations " << allocations << "\n";
    CHECK(peak <= budget);
    CHECK(arena.used() == 0);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    Arena& shorter = qiven_ctx.counted.arena(peak - 1);
    String out{Allocator<char>(shorter)};
    bool refused = false;
    try {
        op(out);
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget;
    }
    CHECK(refused);
    CHECK(out.empty());
#else
    std::cout << "digest." << name << ": a byte short not run, under MSVC's debug containers\n";
#endif
}

}  // namespace

QIVEN_CASE(digest, budgets) {
    const std::string input = data();
    CHECK_EQ(input.size(), 1196U);
    const std::array<std::byte, digest::bytes> key = counting_key();
    budget_case(qiven_ctx, "plain", input.size(), [&](String& out) { digest::plain(input, out); });
    budget_case(qiven_ctx, "keyed", input.size(), [&](String& out) {
        digest::keyed(std::span<const std::byte, digest::bytes>(key), input, out, zero);
    });
}

QIVEN_CASE(digest, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::string input = data();
    const std::array<std::byte, digest::bytes> key = counting_key();
    std::vector<std::byte> pages(budget_for(input.size()));
    qiven::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto runs = [&](qiven::log::Name site, std::string_view name, const auto& write) {
        const qiven::test::FaultRuns r = qiven::test::run_faults<MemoryError>(
            qiven_ctx.log, site, countdown,
            [&] {
                arena.emplace(pages);
                arena->attach(&countdown);
                String out{Allocator<char>(*arena)};
                write(out);
            },
            [&](std::size_t) { return arena->used() == 0; });
        std::cout << "digest.faults: " << name << "'s allocations " << r.typed << ", completed at " << r.completed_at << "\n";
        // The result's one allocation.
        CHECK_EQ(r.typed, 1U);
        CHECK_EQ(r.completed_at, 2U);
        CHECK_EQ(r.untyped, 0U);
        CHECK_EQ(r.unchecked, 0U);
    };
    runs("digest.plain", "plain", [&](String& out) { digest::plain(input, out); });
    runs("digest.keyed", "keyed", [&](String& out) {
        digest::keyed(std::span<const std::byte, digest::bytes>(key), input, out, zero);
    });
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
