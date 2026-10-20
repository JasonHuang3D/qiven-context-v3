#pragma once

// pool: fixed-size slots over the pages a caller takes, freed in any order;
// and Heap, a pool for each size class over one span, SQLite's allocator.
// Each counts the bytes it holds and their high-water mark, exactly.
//
// Under QIVEN3_SYSTEM_HEAP each slot is its own allocation of the system
// heap, freed at its free, so AddressSanitizer bounds and poisons it; the
// span still bounds the count of slots, so the counts and the budget are
// the same as without the option. Without it, under AddressSanitizer, the
// span is poisoned but for the slots held (memory/poison.hpp): each freed
// slot, the slots not yet cut, and every heap block's header.
//
// Pool: thread: one. Heap: thread: shared; its mutex guards its counts, its
// cursor and its free lists.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "memory/memory.hpp"

#if defined(QIVEN3_SYSTEM_HEAP)
#include <unordered_map>
#endif

namespace qiven3::memory {

// Every slot of a pool or a heap starts at a multiple of this.
inline constexpr std::size_t slot_align = 16;
// Heap's size classes: 16 << c bytes for c below this.
inline constexpr std::size_t heap_classes = 32;
// The end of a pool's list of freed slots.
inline constexpr std::size_t no_slot = ~std::size_t{0};

class Pool {
public:
    // Slots of `slot` bytes, rounded up to a multiple of slot_align, as many
    // as the span holds from its first aligned byte.
    Pool(std::size_t slot, std::span<std::byte> pages) noexcept;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;
    ~Pool();

    // One slot; with every slot held, MemoryError budget and nothing
    // changed.
    [[nodiscard]] void* allocate();
    // Gives back a slot of this pool's, found in constant time from its
    // offset in the span; any other pointer, another allocator's, a slot's
    // interior or past the last slot handed out, is a broken invariant.
    void free(void* p) noexcept;

    [[nodiscard]] std::size_t slot() const noexcept { return slot_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t used() const noexcept { return used_; }
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }

#if defined(QIVEN3_FAULTS)
    void attach(Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    std::span<std::byte> pages_;
    std::size_t slot_ = 0;
    std::size_t capacity_ = 0;
    // Slots ever handed out from the span's front, and the freed ones'
    // list, by index; under QIVEN3_SYSTEM_HEAP, their count.
    std::size_t bumped_ = 0;
#if defined(QIVEN3_SYSTEM_HEAP)
    std::size_t free_count_ = 0;
#else
    std::size_t free_head_ = no_slot;
#endif
    std::size_t used_ = 0;
    std::size_t peak_ = 0;
#if defined(QIVEN3_FAULTS)
    Countdown* countdown_ = nullptr;
#endif
};

// The longest wait for Heap's mutex; past it, allocate and resize give
// null, and free or size fails fast.
inline constexpr std::chrono::seconds heap_wait{5};

class Heap {
public:
    explicit Heap(std::span<std::byte> pages) noexcept;
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&) = delete;
    Heap& operator=(Heap&&) = delete;
    ~Heap();

    // n bytes (one if n is 0) at a multiple of slot_align; null past the
    // budget, as SQLite's allocator gives it, nothing changed.
    [[nodiscard]] void* allocate(std::size_t n) noexcept;
    // Gives back a block, found in constant time from its offset in the
    // span; null is none. Any other pointer, or a block given back twice, is
    // a broken invariant.
    void free(void* p) noexcept;
    // The block's bytes in a block of at least n: p itself if its class
    // holds n, else a new block with p's bytes and p freed; null past the
    // budget, p kept. A null p is allocate(n).
    [[nodiscard]] void* resize(void* p, std::size_t n) noexcept;
    // The bytes a block can hold, at least what was asked; 0 for null.
    [[nodiscard]] std::size_t size(const void* p) const noexcept;

    // The bytes of the blocks held, headers included, and their mark.
    [[nodiscard]] std::size_t used() const noexcept;
    [[nodiscard]] std::size_t peak() const noexcept;
    [[nodiscard]] std::size_t budget() const noexcept { return pages_.size(); }

#if defined(QIVEN3_FAULTS)
    void attach(Countdown* countdown) noexcept;
#endif

private:
    // The counts, which every thread writes, 64 bytes, a cache line on
    // x64, from any other member: spaced rather than aligned, since an
    // alignment that pads is a warning, an error here.
    struct Counts {
        std::array<std::byte, 64> before{};
        std::size_t used = 0;
        std::size_t peak = 0;
        std::array<std::byte, 64> after{};
    };

    void hold() const noexcept QIVEN3_ACQUIRE(mutex_);
    void* take(std::size_t n) noexcept QIVEN3_REQUIRES(mutex_);
    void give(void* p) noexcept QIVEN3_REQUIRES(mutex_);
    // A held block's class and its payload's offset in the span (0 under
    // QIVEN3_SYSTEM_HEAP), in constant time; any other pointer fails fast.
    struct Block {
        std::size_t cls = 0;
        std::size_t at = 0;
    };
    Block find(const void* p) const noexcept QIVEN3_REQUIRES(mutex_);

    mutable Mutex mutex_;
    Counts counts_ QIVEN3_GUARDED_BY(mutex_);
    std::span<std::byte> pages_;
    // The front of the span not yet cut into slots.
    std::size_t bump_ QIVEN3_GUARDED_BY(mutex_) = 0;
    // Each class's freed slots: the list's first payload offset, 0 for
    // none, since a header precedes each payload; or, under
    // QIVEN3_SYSTEM_HEAP, their count. A slot is a header of slot_align
    // bytes and its class's bytes.
    std::array<std::size_t, heap_classes> free_ QIVEN3_GUARDED_BY(mutex_){};
#if defined(QIVEN3_SYSTEM_HEAP)
    std::unordered_map<const void*, std::size_t> blocks_ QIVEN3_GUARDED_BY(mutex_);
#endif
#if defined(QIVEN3_FAULTS)
    Countdown* countdown_ QIVEN3_GUARDED_BY(mutex_) = nullptr;
#endif
};

}  // namespace qiven3::memory
