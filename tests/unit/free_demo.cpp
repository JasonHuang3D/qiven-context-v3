// The frees that end the process, run as processes by tests/drive/test_free.py;
// never CTest tests themselves. free.*: a wrong pointer given back to a pool
// or a heap, another allocator's, a block's interior (a heap's with a copied
// header too), one past the last block or past the span, or a block given
// back twice, each a broken invariant; under QIVEN_SYSTEM_HEAP a pool's
// slot is the system heap's, whose AddressSanitizer reports the wrong free
// instead. asan.*: a touch of a freed block, of a heap block's header, of an
// arena's block after the arena's end, past its cursor or in its padding,
// which AddressSanitizer reports, the blocks the system heap's under
// QIVEN_SYSTEM_HEAP, or poisoned in the span (memory/poison.hpp); each
// skipped where no tool would see it. A case that ends prints "not caught";
// one that passes its wrong free prints "past the wrong free" first.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

#include "memory/arena.hpp"
#include "memory/poison.hpp"
#include "memory/pool.hpp"
#include "qiven/runner/runner.hpp"

namespace {

using qiven3::memory::Arena;
using qiven3::memory::Heap;
using qiven3::memory::Pool;

struct alignas(4096) Page {
    std::array<std::byte, 4096> bytes{};
};

// A block's byte at `at`, by a span over the block, never by arithmetic.
void* byte_at(void* block, std::size_t size, std::size_t at) {
    return &std::span<std::byte>(static_cast<std::byte*>(block), size + 1)[at];
}

void past() {
    std::cout << "past the wrong free\n";
    std::cout.flush();
}

void not_caught() {
    std::cout << "not caught\n";
    std::cout.flush();
}

// Whether AddressSanitizer sees a touch of v3's blocks: on the system heap,
// or poisoned in the span.
#if defined(QIVEN_SYSTEM_HEAP) || QIVEN3_POISONING
#define QIVEN3_SEEN 1
#else
#define QIVEN3_SEEN 0
#endif

#if QIVEN3_SEEN
// Reads a byte the process may no longer touch, and prints it, so the read
// stays in the build.
void touch(const void* p) {
    std::cout << static_cast<int>(*static_cast<const unsigned char*>(p)) << "\n";
}
#endif

}  // namespace

#if !QIVEN3_SEEN
constexpr const char* unseen = "not applicable: built without QIVEN_SYSTEM_HEAP or AddressSanitizer";
#endif

QIVEN_CASE(free, pool_foreign) {
    const auto mine = std::make_unique<Page>();
    const auto theirs = std::make_unique<Page>();
    Pool pool(48, mine->bytes);
    Pool other(48, theirs->bytes);
    void* held = pool.allocate();
    void* p = other.allocate();
    pool.free(p);
    past();
    other.free(p);
    pool.free(held);
    not_caught();
}

QIVEN_CASE(free, pool_interior) {
    const auto pages = std::make_unique<Page>();
    Pool pool(48, pages->bytes);
    void* p = pool.allocate();
    pool.free(byte_at(p, 48, 16));
    past();
    pool.free(p);
    not_caught();
}

QIVEN_CASE(free, pool_one_past) {
    // One past the one slot handed out: the next slot's place, not yet cut.
    const auto pages = std::make_unique<Page>();
    Pool pool(48, pages->bytes);
    void* p = pool.allocate();
    pool.free(byte_at(p, 48, 48));
    past();
    pool.free(p);
    not_caught();
}

QIVEN_CASE(free, pool_past_span) {
    const auto pages = std::make_unique<Page>();
    Pool pool(4096, pages->bytes);
    void* p = pool.allocate();
    pool.free(byte_at(p, 4096, 4096));
    past();
    pool.free(p);
    not_caught();
}

QIVEN_CASE(free, heap_foreign) {
    const auto mine = std::make_unique<Page>();
    const auto theirs = std::make_unique<Page>();
    Heap heap(mine->bytes);
    Heap other(theirs->bytes);
    void* held = heap.allocate(100);
    void* p = other.allocate(100);
    heap.free(p);
    past();
    other.free(p);
    heap.free(held);
    not_caught();
}

QIVEN_CASE(free, heap_interior) {
    const auto pages = std::make_unique<Page>();
    Heap heap(pages->bytes);
    void* p = heap.allocate(128);
    for (std::size_t i = 0; i < 128; ++i) {
        *static_cast<std::byte*>(byte_at(p, 128, i)) = std::byte{0x5a};
    }
    heap.free(byte_at(p, 128, 16));
    past();
    heap.free(p);
    not_caught();
}

QIVEN_CASE(free, heap_interior_forged) {
    // An interior pointer whose preceding bytes copy a held block's header,
    // its class and its state, but not its own offset.
    const auto pages = std::make_unique<Page>();
    Heap heap(pages->bytes);
    void* p = heap.allocate(128);
    constexpr std::array<std::uint32_t, 4> forged{0, 0x48454c44U, 0, 0};
    std::memcpy(p, forged.data(), sizeof forged);
    heap.free(byte_at(p, 128, 16));
    past();
    heap.free(p);
    not_caught();
}

QIVEN_CASE(free, heap_one_past) {
    // One past the last block: the front of the span not yet cut.
    const auto pages = std::make_unique<Page>();
    Heap heap(pages->bytes);
    void* p = heap.allocate(128);
    heap.free(byte_at(p, 128, 128));
    past();
    heap.free(p);
    not_caught();
}

QIVEN_CASE(free, heap_past_span) {
    // The span's one block of 2,048 bytes ends 2,032 bytes short of its end.
    const auto pages = std::make_unique<Page>();
    Heap heap(pages->bytes);
    void* p = heap.allocate(2048);
    heap.free(byte_at(p, 4096 - 16, 4096 - 16));
    past();
    heap.free(p);
    not_caught();
}

QIVEN_CASE(free, heap_twice) {
    const auto pages = std::make_unique<Page>();
    Heap heap(pages->bytes);
    void* p = heap.allocate(100);
    heap.free(p);
    heap.free(p);
    past();
    not_caught();
}

QIVEN_CASE(asan, pool_use_after_free) {
#if QIVEN3_SEEN
    const auto pages = std::make_unique<Page>();
    Pool pool(48, pages->bytes);
    void* p = pool.allocate();
    pool.free(p);
    touch(p);
    not_caught();
#else
    QIVEN_SKIP(unseen);
#endif
}

QIVEN_CASE(asan, heap_use_after_free) {
#if QIVEN3_SEEN
    const auto pages = std::make_unique<Page>();
    Heap heap(pages->bytes);
    void* p = heap.allocate(100);
    heap.free(p);
    touch(p);
    not_caught();
#else
    QIVEN_SKIP(unseen);
#endif
}

QIVEN_CASE(asan, heap_header) {
    // The first block's header, the span's first 16 bytes, its payload after.
#if QIVEN3_POISONING
    const auto pages = std::make_unique<Page>();
    Heap heap(pages->bytes);
    void* p = heap.allocate(100);
    touch(&pages->bytes[8]);
    heap.free(p);
    not_caught();
#else
    QIVEN_SKIP("not applicable: a block's header is in the span, poisoned, only under "
                "AddressSanitizer without QIVEN_SYSTEM_HEAP");
#endif
}

QIVEN_CASE(asan, arena_use_after_end) {
#if defined(QIVEN_SYSTEM_HEAP)
    const auto pages = std::make_unique<Page>();
    void* p = nullptr;
    {
        Arena arena(pages->bytes);
        p = arena.allocate(100, 16);
    }
    touch(p);
    not_caught();
#else
    QIVEN_SKIP("not applicable: the block is the system heap's only under QIVEN_SYSTEM_HEAP; in the "
                "span, the arena's end gives the pages back to their owner unpoisoned");
#endif
}

QIVEN_CASE(asan, arena_past_block) {
    // The arena's unused span past a block: past its cursor.
#if QIVEN3_SEEN
    const auto pages = std::make_unique<Page>();
    Arena arena(pages->bytes);
    void* p = arena.allocate(100, 16);
    touch(byte_at(p, 100, 100));
    arena.deallocate(p, 100);
    not_caught();
#else
    QIVEN_SKIP(unseen);
#endif
}

QIVEN_CASE(asan, arena_padding) {
    // The padding between a block of 100 bytes and the next at 112.
#if QIVEN3_SEEN
    const auto pages = std::make_unique<Page>();
    Arena arena(pages->bytes);
    void* p = arena.allocate(100, 16);
    void* q = arena.allocate(16, 16);
    touch(byte_at(p, 112, 104));
    arena.deallocate(q, 16);
    arena.deallocate(p, 100);
    not_caught();
#else
    QIVEN_SKIP(unseen);
#endif
}
