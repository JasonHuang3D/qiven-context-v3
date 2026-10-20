// arena's cases: its bounds and the budget's error, alignment, the exact
// count and its mark, and each allocation of an operation failed in turn.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using qiven3::memory::Vector;

struct alignas(4096) Pages {
    std::array<std::byte, 4096> bytes{};
};

struct alignas(8192) Two {
    std::array<std::byte, 8192> bytes{};
};

std::uintptr_t address(const void* p) { return std::bit_cast<std::uintptr_t>(p); }

}  // namespace

QIVEN3_CASE(arena, bounds_budget) {
    const auto pages = std::make_unique<Pages>();
    Arena arena(std::span<std::byte>(pages->bytes).first(64));
    CHECK_EQ(arena.budget(), 64U);
    void* p = arena.allocate(64, 1);
#if !defined(QIVEN3_SYSTEM_HEAP)
    CHECK(p == static_cast<void*>(pages->bytes.data()));
#endif
    CHECK_EQ(arena.used(), 64U);
    bool refused = false;
    try {
        static_cast<void>(arena.allocate(1, 1));
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget && e.operation.view() == "arena" &&
                  e.detail.view() == "budget 64, request 1, held 64";
    }
    CHECK(refused);
    // Nothing changed by the refusal; a release keeps the cursor.
    CHECK_EQ(arena.used(), 64U);
    CHECK_EQ(arena.peak(), 64U);
    arena.deallocate(p, 64);
    CHECK_EQ(arena.used(), 0U);
    CHECK_EQ(arena.peak(), 64U);
    bool still = false;
    try {
        static_cast<void>(arena.allocate(1, 1));
    } catch (const MemoryError&) {
        still = true;
    }
    CHECK(still);
}

QIVEN3_CASE(arena, a_byte_short) {
    const auto pages = std::make_unique<Pages>();
    Arena arena(std::span<std::byte>(pages->bytes).first(100));
    static_cast<void>(arena.allocate(99, 1));
    bool refused = false;
    try {
        static_cast<void>(arena.allocate(2, 1));
    } catch (const MemoryError& e) {
        refused = e.detail.view() == "budget 100, request 2, held 99";
    }
    CHECK(refused);
    static_cast<void>(arena.allocate(1, 1));
    CHECK_EQ(arena.peak(), 100U);
    // An empty span refuses every request.
    Arena none(std::span<std::byte>{});
    bool empty = false;
    try {
        static_cast<void>(none.allocate(0, 1));
    } catch (const MemoryError& e) {
        empty = e.detail.view() == "budget 0, request 1, held 0";
    }
    CHECK(empty);
}

QIVEN3_CASE(arena, alignment) {
    const auto pages = std::make_unique<Two>();
    Arena arena(pages->bytes);
    void* a = arena.allocate(1, 1);
    void* b = arena.allocate(8, 64);
    void* c = arena.allocate(3, 2);
    CHECK_EQ(arena.peak(), 75U);
    void* d = arena.allocate(16, 4096);
#if !defined(QIVEN3_SYSTEM_HEAP)
    CHECK_EQ(address(a) - address(pages->bytes.data()), 0U);
    CHECK_EQ(address(b) - address(pages->bytes.data()), 64U);
    CHECK_EQ(address(c) - address(pages->bytes.data()), 72U);
    CHECK_EQ(address(d) - address(pages->bytes.data()), 4096U);
#endif
    CHECK_EQ(address(b) % 64, 0U);
    CHECK_EQ(address(c) % 2, 0U);
    CHECK_EQ(address(d) % 4096, 0U);
    // The count is the bytes asked; the mark the padding too. Under
    // QIVEN3_SYSTEM_HEAP the blocks are the system heap's, and the cursor
    // still runs through the span.
    CHECK_EQ(arena.used(), 28U);
    CHECK_EQ(arena.peak(), 4112U);
    bool refused = false;
    try {
        static_cast<void>(arena.allocate(1, 8192));
    } catch (const MemoryError& e) {
        refused = e.detail.view() == "budget 8192, request 1, held 4112";
    }
    CHECK(refused);
    arena.deallocate(a, 1);
    arena.deallocate(b, 8);
    arena.deallocate(c, 3);
    CHECK_EQ(arena.used(), 16U);
    arena.deallocate(d, 16);
    CHECK_EQ(arena.used(), 0U);
}

QIVEN3_CASE(arena, used_peak) {
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    CHECK_EQ(arena.used(), 0U);
    CHECK_EQ(arena.peak(), 0U);
    void* a = arena.allocate(100, 4);
    void* b = arena.allocate(0, 1);
    CHECK_EQ(arena.used(), 101U);
    CHECK_EQ(arena.peak(), 101U);
    arena.deallocate(a, 100);
    CHECK_EQ(arena.used(), 1U);
    void* c = arena.allocate(10, 8);
    CHECK_EQ(arena.used(), 11U);
    CHECK_EQ(arena.peak(), 114U);
    arena.deallocate(c, 10);
    arena.deallocate(b, 0);
    CHECK_EQ(arena.used(), 0U);
    CHECK_EQ(arena.peak(), 114U);
}

QIVEN3_CASE(arena, faults) {
#if defined(QIVEN3_FAULTS)
    const auto pages = std::make_unique<Pages>();
    qiven3::memory::Countdown countdown;
    // Each run is an operation with an arena of its own over the same pages,
    // which the check reads after the run.
    std::optional<Arena> arena;
    const auto fresh = [&]() -> Arena& {
        arena.emplace(pages->bytes);
        arena->attach(&countdown);
        return *arena;
    };
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    // Five blocks through the allocator, each given back on any exit.
    const auto blocks = [&] {
        Allocator<long long> allocator(fresh());
        std::array<long long*, 5> held{};
        try {
            for (std::size_t i = 0; i < held.size(); ++i) {
                held[i] = allocator.allocate(i + 1);
            }
        } catch (...) {
            for (std::size_t i = 0; i < held.size(); ++i) {
                if (held[i] != nullptr) {
                    allocator.deallocate(held[i], i + 1);
                }
            }
            throw;
        }
        for (std::size_t i = 0; i < held.size(); ++i) {
            allocator.deallocate(held[i], i + 1);
        }
    };
    const qiven3::test::FaultRuns raw = qiven3::test::run_faults<MemoryError>(countdown, blocks, check);
    CHECK_EQ(raw.typed, 5U);
    CHECK_EQ(raw.completed_at, 6U);
    CHECK_EQ(raw.untyped, 0U);
    CHECK_EQ(raw.unchecked, 0U);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    // Ten strings, each past the small-string buffer, in a vector, all on
    // the arena. MSVC's debug containers allocate in their noexcept
    // constructors and moves, where a failure ends the process, so this
    // half runs where its iterators do not debug: asan, CI.
    std::size_t length = 0;
    const auto strings = [&] {
        Arena& one = fresh();
        Vector<String> v(Allocator<String>{one});
        for (int i = 0; i < 10; ++i) {
            v.emplace_back(40, 'a', Allocator<char>{one});
        }
        length = v.size() * v.front().size();
    };
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<MemoryError>(countdown, strings, check);
    std::cout << "arena.faults: typed " << runs.typed << ", completed at " << runs.completed_at
              << "\n";
    CHECK(runs.completed_at > 1);
    CHECK_EQ(runs.typed, runs.completed_at - 1);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(length, 400U);
#endif
    CHECK_EQ(arena->used(), 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS");
#endif
}
