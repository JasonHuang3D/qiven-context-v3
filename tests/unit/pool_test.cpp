// pool's cases: a pool's slots freed in any order and its budget; Heap's
// allocate, free, resize and size, its budget, its threads, and each
// allocation of an operation failed in turn.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <span>
#include <thread>
#include <vector>

#include "memory/memory.hpp"
#include "memory/pool.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

using qiven3::memory::Heap;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::Pool;

struct alignas(4096) Pages {
    std::array<std::byte, 4096> bytes{};
};

std::uintptr_t address(const void* p) { return std::bit_cast<std::uintptr_t>(p); }

// A slot of a pool or heap: 16 << c bytes after a 16-byte header.
constexpr std::size_t heap_slot(std::size_t payload) { return 16 + payload; }

}  // namespace

QIVEN3_CASE(pool, any_order) {
    const auto pages = std::make_unique<Pages>();
    // From the span's second byte: the pool starts at its first aligned one.
    Pool pool(24, std::span<std::byte>(pages->bytes).subspan(1, 1024));
    CHECK_EQ(pool.slot(), 32U);
    CHECK_EQ(pool.capacity(), 31U);
    std::vector<void*> slots;
    for (std::size_t i = 0; i < pool.capacity(); ++i) {
        slots.push_back(pool.allocate());
        CHECK_EQ(address(slots.back()) % 16, 0U);
    }
    CHECK_EQ(std::set<void*>(slots.begin(), slots.end()).size(), slots.size());
    CHECK_EQ(pool.used(), 31U * 32U);
    bool refused = false;
    try {
        static_cast<void>(pool.allocate());
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget && e.operation.view() == "pool" &&
                  e.detail.view() == "budget 992, request 32, held 992";
    }
    CHECK(refused);
    CHECK_EQ(pool.used(), 992U);
    // Freed in an order of their own: odd ones, then the rest backwards.
    for (std::size_t i = 1; i < slots.size(); i += 2) {
        pool.free(slots[i]);
    }
    for (std::size_t i = slots.size(); i > 0; --i) {
        if ((i - 1) % 2 == 0) {
            pool.free(slots[i - 1]);
        }
    }
    CHECK_EQ(pool.used(), 0U);
    CHECK_EQ(pool.peak(), 992U);
    // Each slot serves again, and every one is the span's.
    std::vector<void*> again;
    for (std::size_t i = 0; i < pool.capacity(); ++i) {
        again.push_back(pool.allocate());
    }
#if !defined(QIVEN_SYSTEM_HEAP)
    std::sort(slots.begin(), slots.end(), std::less<void*>());
    std::sort(again.begin(), again.end(), std::less<void*>());
    CHECK(slots == again);
#endif
    for (void* p : again) {
        pool.free(p);
    }
    CHECK_EQ(pool.used(), 0U);
}

QIVEN3_CASE(pool, faults) {
#if defined(QIVEN_FAULTS)
    const auto pages = std::make_unique<Pages>();
    Pool pool(64, pages->bytes);
    qiven3::memory::Countdown countdown;
    pool.attach(&countdown);
    // The operation takes five slots and gives them back, on success or
    // failure alike.
    const auto op = [&] {
        std::vector<void*> held;
        held.reserve(5);
        try {
            for (int i = 0; i < 5; ++i) {
                held.push_back(pool.allocate());
            }
        } catch (...) {
            for (void* p : held) {
                pool.free(p);
            }
            throw;
        }
        for (void* p : held) {
            pool.free(p);
        }
    };
    const auto check = [&](std::size_t) { return pool.used() == 0; };
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<MemoryError>(
        qiven3_ctx.log, "pool.faults", countdown, op, check);
    CHECK_EQ(runs.typed, 5U);
    CHECK_EQ(runs.completed_at, 6U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    pool.attach(nullptr);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}

QIVEN3_CASE(heap, allocate_free_size) {
    const auto pages = std::make_unique<Pages>();
    Heap heap(pages->bytes);
    CHECK_EQ(heap.budget(), 4096U);
    void* a = heap.allocate(1);
    void* b = heap.allocate(16);
    void* c = heap.allocate(17);
    void* d = heap.allocate(0);
    void* e = heap.allocate(1000);
    REQUIRE(a != nullptr && b != nullptr && c != nullptr && d != nullptr && e != nullptr);
    CHECK_EQ(heap.size(a), 16U);
    CHECK_EQ(heap.size(b), 16U);
    CHECK_EQ(heap.size(c), 32U);
    CHECK_EQ(heap.size(d), 16U);
    CHECK_EQ(heap.size(e), 1024U);
    CHECK_EQ(heap.size(nullptr), 0U);
    for (void* p : {a, b, c, d, e}) {
        CHECK_EQ(address(p) % 16, 0U);
    }
    const std::size_t all = 3 * heap_slot(16) + heap_slot(32) + heap_slot(1024);
    CHECK_EQ(heap.used(), all);
    CHECK_EQ(heap.peak(), all);
    std::memset(e, 0x5a, 1024);
    heap.free(c);
    heap.free(a);
    heap.free(nullptr);
    CHECK_EQ(heap.used(), 2 * heap_slot(16) + heap_slot(1024));
    // A freed slot serves its class again.
    void* f = heap.allocate(10);
#if !defined(QIVEN_SYSTEM_HEAP)
    CHECK(f == a);
#endif
    heap.free(e);
    heap.free(b);
    heap.free(d);
    heap.free(f);
    CHECK_EQ(heap.used(), 0U);
    CHECK_EQ(heap.peak(), all);
}

QIVEN3_CASE(heap, resize) {
    const auto pages = std::make_unique<Pages>();
    Heap heap(pages->bytes);
    auto* p = static_cast<unsigned char*>(heap.allocate(20));
    REQUIRE(p != nullptr);
    const std::span<unsigned char> first(p, 20);
    for (unsigned char i = 0; i < 20; ++i) {
        first[i] = i;
    }
    // Within its class, the same block.
    CHECK(heap.resize(p, 32) == p);
    CHECK_EQ(heap.used(), heap_slot(32));
    auto* q = static_cast<unsigned char*>(heap.resize(p, 100));
    REQUIRE(q != nullptr);
    CHECK_EQ(heap.size(q), 128U);
    const std::span<unsigned char> moved(q, 20);
    bool same = true;
    for (unsigned char i = 0; i < 20; ++i) {
        same = same && moved[i] == i;
    }
    CHECK(same);
    CHECK_EQ(heap.used(), heap_slot(128));
    // Past the budget: null, the block kept.
    CHECK(heap.resize(q, 4096) == nullptr);
    CHECK_EQ(heap.size(q), 128U);
    CHECK_EQ(heap.used(), heap_slot(128));
    // Smaller, into a smaller class.
    void* r = heap.resize(q, 8);
    REQUIRE(r != nullptr);
    CHECK_EQ(heap.size(r), 16U);
    CHECK_EQ(std::span<unsigned char>(static_cast<unsigned char*>(r), 8)[7], 7);
    void* s = heap.resize(nullptr, 40);
    CHECK_EQ(heap.size(s), 64U);
    heap.free(r);
    heap.free(s);
    CHECK_EQ(heap.used(), 0U);
    CHECK_EQ(heap.peak(), heap_slot(32) + heap_slot(128));
}

QIVEN3_CASE(heap, budget) {
    const auto pages = std::make_unique<Pages>();
    Heap heap(pages->bytes);
    // 4096 bytes hold 85 slots of 48 bytes, and 16 left over.
    std::vector<void*> held;
    for (void* p = heap.allocate(32); p != nullptr; p = heap.allocate(32)) {
        held.push_back(p);
    }
    CHECK_EQ(held.size(), 85U);
    CHECK_EQ(heap.used(), 85U * 48U);
    CHECK(heap.allocate(1) == nullptr);
    CHECK(heap.allocate(std::size_t{1} << 40) == nullptr);
    heap.free(held[40]);
    // A freed slot serves its own class alone.
    CHECK(heap.allocate(64) == nullptr);
    void* again = heap.allocate(17);
    CHECK(again != nullptr);
    heap.free(again);
    for (void* p : held) {
        if (p != held[40]) {
            heap.free(p);
        }
    }
    CHECK_EQ(heap.used(), 0U);
    CHECK_EQ(heap.peak(), 85U * 48U);
}

QIVEN3_CASE(heap, threads) {
    std::vector<std::byte> bytes(std::size_t{8} << 20);
    Heap heap(bytes);
    std::atomic<std::size_t> nulls{0};
    {
        // std::thread, joined here: libc++ before 20 keeps std::jthread
        // experimental.
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&heap, &nulls, t] {
                std::vector<void*> held;
                for (int i = 0; i < 2000; ++i) {
                    // 8 MiB holds every block: none is null.
                    held.push_back(heap.allocate(static_cast<std::size_t>(8 + (i * 7 + t) % 300)));
                    if (held.back() == nullptr) {
                        nulls.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (i % 3 == 0) {
                        heap.free(held.back());
                        held.pop_back();
                    }
                }
                for (void* p : held) {
                    heap.free(p);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }
    CHECK_EQ(nulls.load(std::memory_order_relaxed), 0U);
    CHECK_EQ(heap.used(), 0U);
    CHECK(heap.peak() > 0U);
}

QIVEN3_CASE(heap, faults) {
#if defined(QIVEN_FAULTS)
    const auto pages = std::make_unique<Pages>();
    Heap heap(pages->bytes);
    qiven3::memory::Countdown countdown;
    heap.attach(&countdown);
    // As the store maps SQLite's SQLITE_NOMEM: a null block is the budget's
    // error, and what the operation took is given back.
    const auto op = [&] {
        std::array<void*, 4> held{};
        for (std::size_t i = 0; i < held.size(); ++i) {
            held[i] = heap.allocate(100 * (i + 1));
            if (held[i] == nullptr) {
                for (void* p : held) {
                    heap.free(p);
                }
                throw MemoryError::budget("heap", heap.budget(), 100 * (i + 1), heap.used());
            }
        }
        void* grown = heap.resize(held[0], 1000);
        if (grown == nullptr) {
            for (void* p : held) {
                heap.free(p);
            }
            throw MemoryError::budget("heap", heap.budget(), 1000, heap.used());
        }
        held[0] = grown;
        for (void* p : held) {
            heap.free(p);
        }
    };
    const auto check = [&](std::size_t) { return heap.used() == 0; };
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<MemoryError>(
        qiven3_ctx.log, "heap.faults", countdown, op, check);
    CHECK_EQ(runs.typed, 5U);
    CHECK_EQ(runs.completed_at, 6U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    heap.attach(nullptr);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
