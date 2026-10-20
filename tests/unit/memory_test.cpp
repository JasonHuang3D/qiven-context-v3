// memory's cases: checked sizes, bounded text, the budget's error, the
// allocator and its containers on an arena, the lock and the countdown.

#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <limits>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "memory/pool.hpp"
#include "runner/runner.hpp"

namespace {

using qiven3::memory::add;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::Inline;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::mul;
using qiven3::memory::String;
using qiven3::memory::Vector;

constexpr std::size_t top = std::numeric_limits<std::size_t>::max();

// Constant-evaluated: a fault here fails the build.
static_assert(add(2, 3) == std::optional<std::size_t>{5});
static_assert(add(top, 0) == std::optional<std::size_t>{top});
static_assert(!add(top, 1).has_value());
static_assert(!add(1, top).has_value());
static_assert(mul(0, top) == std::optional<std::size_t>{0});
static_assert(mul(top, 1) == std::optional<std::size_t>{top});
static_assert(!mul(top / 2 + 1, 2).has_value());
static_assert(mul(top / 2, 2) == std::optional<std::size_t>{top - 1});
static_assert(Inline<4>("abcdef").view() == "abcd");
static_assert(Inline<2>("a\xC3\xA9").view() == "a");

// The bytes of one operation, aligned as pages are.
struct alignas(4096) Pages {
    std::array<std::byte, 8192> bytes{};
};

}  // namespace

QIVEN3_CASE(memory, add_mul) {
    CHECK(add(top - 1, 1) == std::optional<std::size_t>{top});
    CHECK(!add(top - 1, 2).has_value());
    CHECK(mul(top / 3, 3) == std::optional<std::size_t>{top / 3 * 3});
    CHECK(!mul(top / 3 + 1, 4).has_value());
    CHECK(mul(std::size_t{1} << 32, std::size_t{1} << 31) ==
          std::optional<std::size_t>{std::size_t{1} << 63});
    CHECK(!mul(std::size_t{1} << 32, std::size_t{1} << 32).has_value());
}

QIVEN3_CASE(memory, inline_cut) {
    CHECK_EQ(Inline<8>("").size(), 0U);
    CHECK_EQ(Inline<3>("abc").view(), "abc");
    // A two-byte, a three-byte and a four-byte sequence, each cut before.
    CHECK_EQ(Inline<2>("a\xC3\xA9").view(), "a");
    CHECK_EQ(Inline<3>("a\xC3\xA9").view(), "a\xC3\xA9");
    CHECK_EQ(Inline<3>("a\xE2\x82\xAC").view(), "a");
    CHECK_EQ(Inline<4>("a\xE2\x82\xAC").view(), "a\xE2\x82\xAC");
    CHECK_EQ(Inline<4>("a\xF0\x9F\x98\x80").view(), "a");
    CHECK_EQ(Inline<5>("a\xF0\x9F\x98\x80z").view(), "a\xF0\x9F\x98\x80");
}

QIVEN3_CASE(memory, error_budget) {
    const MemoryError e = MemoryError::budget("arena", 65536, 70000, top);
    CHECK(e.code == MemoryCode::budget);
    CHECK_EQ(qiven3::memory::name(e.code), "budget");
    CHECK_EQ(e.operation.view(), "arena");
    CHECK_EQ(e.detail.view(), "budget 65536, request 70000, held 18446744073709551615");
    static_assert(std::is_nothrow_copy_constructible_v<MemoryError>);
}

QIVEN3_CASE(memory, allocator) {
    const auto a = std::make_unique<Pages>();
    const auto b = std::make_unique<Pages>();
    Arena one(a->bytes);
    Arena two(b->bytes);
    const Allocator<int> x(one);
    const Allocator<int> y(one);
    const Allocator<long long> rebound(x);
    CHECK(x == y);
    CHECK(x == rebound);
    CHECK(!(x == Allocator<int>(two)));
    CHECK(&rebound.arena() == &one);
    using Traits = std::allocator_traits<Allocator<int>>;
    static_assert(Traits::propagate_on_container_move_assignment::value);
    static_assert(!Traits::is_always_equal::value);
    int* p = Allocator<int>(one).allocate(10);
    CHECK_EQ(one.used(), 40U);
    Allocator<int>(one).deallocate(p, 10);
    CHECK_EQ(one.used(), 0U);
    CHECK_EQ(one.peak(), 40U);
    // A count whose bytes overflow asks for every byte: the budget's error.
    bool refused = false;
    try {
        static_cast<void>(Allocator<int>(one).allocate(top / 2));
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget;
    }
    CHECK(refused);
    CHECK_EQ(one.peak(), 40U);
}

QIVEN3_CASE(memory, string_on_arena) {
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String s(Allocator<char>{arena});
        s.assign(100, 'x');
        s += "tail";
        CHECK_EQ(s.size(), 104U);
        CHECK_EQ(std::string_view(s).substr(100), "tail");
        CHECK(arena.used() > 104U);
        String moved(std::move(s));
        CHECK(&moved.get_allocator().arena() == &arena);
        CHECK_EQ(moved.size(), 104U);
    }
    CHECK_EQ(arena.used(), 0U);
    CHECK(arena.peak() >= 105U);
}

QIVEN3_CASE(memory, vector_on_arena) {
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        Vector<int> v(Allocator<int>{arena});
        v.reserve(100);
        const std::size_t reserved = arena.used();
        for (int i = 0; i < 100; ++i) {
            v.push_back(i);
        }
        CHECK_EQ(arena.used(), reserved);
        CHECK_EQ(v[99], 99);
        Vector<int> other(Allocator<int>{arena});
        other = std::move(v);
        CHECK_EQ(other.size(), 100U);
        CHECK(other.get_allocator() == Allocator<int>(arena));
    }
    CHECK_EQ(arena.used(), 0U);
}

QIVEN3_CASE(memory, container_budget) {
    std::array<std::byte, 256> bytes{};
    Arena arena(bytes);
    Vector<int> v(Allocator<int>{arena});
    v.reserve(16);
    v.assign(16, 7);
    const std::size_t used = arena.used();
    const std::size_t peak = arena.peak();
    bool refused = false;
    try {
        v.reserve(1000);
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget && e.operation.view() == "arena";
    }
    CHECK(refused);
    // The strong guarantee: the vector and the counts as before.
    CHECK_EQ(v.size(), 16U);
    CHECK_EQ(v[15], 7);
    CHECK_EQ(arena.used(), used);
    CHECK_EQ(arena.peak(), peak);
}

QIVEN3_CASE(memory, mutex_deadline) {
    qiven3::memory::Mutex mutex;
    REQUIRE(mutex.try_lock_for(std::chrono::milliseconds(10)));
    bool other = true;
    {
        std::jthread waiter([&] {
            other = mutex.try_lock_for(std::chrono::milliseconds(20));
            if (other) {
                mutex.unlock();
            }
        });
    }
    CHECK(!other);
    mutex.unlock();
    {
        std::jthread waiter([&] {
            other = mutex.try_lock_for(std::chrono::milliseconds(1000));
            if (other) {
                mutex.unlock();
            }
        });
    }
    CHECK(other);
}

QIVEN3_CASE(memory, countdown) {
#if defined(QIVEN3_FAULTS)
    qiven3::memory::Countdown countdown;
    CHECK(!countdown.fail());
    countdown.arm(3);
    CHECK(!countdown.fail());
    CHECK(!countdown.fail());
    CHECK(!countdown.fired());
    CHECK(countdown.fail());
    CHECK(countdown.fired());
    CHECK(!countdown.fail());
    CHECK_EQ(countdown.calls(), 4U);
    countdown.disarm();
    CHECK(!countdown.fired());
    CHECK(!countdown.fail());
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS");
#endif
}

// The sizes, for the record: each pinned one is a static_assert beside its
// type, checked where the build's options and library give the pinned
// layout.
QIVEN3_CASE(memory, sizes) {
    std::cout << "Allocator<char> " << sizeof(Allocator<char>) << "\nString " << sizeof(String)
              << "\nVector<int> " << sizeof(Vector<int>) << "\nArena " << sizeof(Arena)
              << "\nPool " << sizeof(qiven3::memory::Pool) << "\nHeap "
              << sizeof(qiven3::memory::Heap) << "\nMemoryError " << sizeof(MemoryError)
              << "\nMutex " << sizeof(qiven3::memory::Mutex) << "\n";
    CHECK_EQ(sizeof(Allocator<char>), 8U);
}
