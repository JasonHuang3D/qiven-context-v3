// The frees of pool and Heap: a block given back from every position of a
// pool's span and of a heap class's (the first, the last, an interior one,
// then the rest in a mixed order), over spans 64 times apart, each freed
// block serving again; and a heap of mixed classes freed the same way.
// tests/drive/test_free.py runs the wrong pointers, which end the process.

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <set>
#include <span>
#include <vector>

#include "memory/pool.hpp"
#include "qiven/runner/runner.hpp"

namespace {

using qiven3::memory::Heap;
using qiven3::memory::Pool;

struct alignas(4096) Page {
    std::array<std::byte, 4096> bytes{};
};

// The two spans' sizes, in pages: 4 KiB and 256 KiB.
constexpr std::array<std::size_t, 2> spans{1, 64};

std::span<std::byte> bytes_of(std::vector<Page>& pages) {
    return std::as_writable_bytes(std::span<Page>(pages));
}

// The order of the frees over n blocks: the first, the last, an interior
// one, then the rest by a stride of 7, which visits every index of n once
// when 7 does not divide n, and the leftovers in turn.
std::vector<std::size_t> free_order(std::size_t n) {
    std::vector<std::size_t> order;
    std::vector<bool> taken(n, false);
    const auto take = [&](std::size_t i) {
        if (!taken[i]) {
            taken[i] = true;
            order.push_back(i);
        }
    };
    take(0);
    take(n - 1);
    take(n / 2);
    for (std::size_t k = 0, i = 3 % n; k < n; ++k, i = (i + 7) % n) {
        take(i);
    }
    for (std::size_t i = 0; i < n; ++i) {
        take(i);
    }
    return order;
}

}  // namespace

QIVEN_CASE(pool, free_every_position) {
    for (const std::size_t count : spans) {
        std::vector<Page> pages(count);
        Pool pool(48, bytes_of(pages));
        const std::size_t capacity = count * 4096 / 48;
        CHECK_EQ(pool.capacity(), capacity);
        std::vector<void*> slots;
        for (std::size_t i = 0; i < capacity; ++i) {
            slots.push_back(pool.allocate());
        }
        const std::vector<std::size_t> order = free_order(capacity);
        CHECK_EQ(order.size(), capacity);
        std::size_t held = capacity;
        for (const std::size_t i : order) {
            pool.free(slots[i]);
            --held;
            CHECK_EQ(pool.used(), held * 48);
            // The slot just freed serves the next request, and goes back.
            void* again = pool.allocate();
#if !defined(QIVEN_SYSTEM_HEAP)
            CHECK(again == slots[i]);
#endif
            pool.free(again);
            CHECK_EQ(pool.used(), held * 48);
        }
        CHECK_EQ(pool.used(), 0U);
        CHECK_EQ(pool.peak(), capacity * 48);
        // Every slot serves again, each one of the span's.
        std::vector<void*> again;
        for (std::size_t i = 0; i < capacity; ++i) {
            again.push_back(pool.allocate());
        }
        CHECK_EQ(std::set<void*>(again.begin(), again.end()).size(), capacity);
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
}

QIVEN_CASE(heap, free_every_position) {
    // One class: 100 bytes in 128, a slot of 144 with its header.
    for (const std::size_t count : spans) {
        std::vector<Page> pages(count);
        Heap heap(bytes_of(pages));
        std::vector<void*> blocks;
        for (void* p = heap.allocate(100); p != nullptr; p = heap.allocate(100)) {
            blocks.push_back(p);
        }
        CHECK_EQ(blocks.size(), count * 4096 / 144);
        std::size_t held = blocks.size();
        for (const std::size_t i : free_order(blocks.size())) {
            CHECK_EQ(heap.size(blocks[i]), 128U);
            heap.free(blocks[i]);
            --held;
            CHECK_EQ(heap.used(), held * 144);
            void* again = heap.allocate(128);
#if !defined(QIVEN_SYSTEM_HEAP)
            CHECK(again == blocks[i]);
#endif
            heap.free(again);
        }
        CHECK_EQ(heap.used(), 0U);
        CHECK_EQ(heap.peak(), blocks.size() * 144);
    }
}

QIVEN_CASE(heap, free_mixed_classes) {
    // Six classes in turn, 16 to 512 bytes, until the span is spent.
    constexpr std::array<std::size_t, 6> asked{1, 17, 33, 65, 129, 257};
    constexpr std::array<std::size_t, 6> payload{16, 32, 64, 128, 256, 512};
    for (const std::size_t count : spans) {
        std::vector<Page> pages(count);
        Heap heap(bytes_of(pages));
        std::vector<void*> blocks;
        std::vector<std::size_t> classes;
        std::size_t total = 0;
        for (std::size_t k = 0;; ++k) {
            void* p = heap.allocate(asked[k % asked.size()]);
            if (p == nullptr) {
                break;
            }
            blocks.push_back(p);
            classes.push_back(k % asked.size());
            total += 16 + payload[k % asked.size()];
        }
        CHECK(blocks.size() > asked.size());
        CHECK_EQ(heap.used(), total);
        for (const std::size_t i : free_order(blocks.size())) {
            CHECK_EQ(heap.size(blocks[i]), payload[classes[i]]);
            heap.free(blocks[i]);
            total -= 16 + payload[classes[i]];
            CHECK_EQ(heap.used(), total);
        }
        CHECK_EQ(heap.used(), 0U);
    }
}
