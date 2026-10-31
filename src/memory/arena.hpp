#pragma once

// arena: the memory of one operation, over the pages its caller takes from
// the platform. It bumps a cursor through its span and gives everything back
// at its end; its span is the operation's budget, so it never grows, and a
// request past it is MemoryError budget with nothing changed.
//
// Under QIVEN_SYSTEM_HEAP each block is its own allocation of the system
// heap, returned at its release or at the arena's end, so AddressSanitizer
// bounds and poisons it; the cursor still runs through the span, so the
// counts and the budget are the same as without the option. Without it,
// under AddressSanitizer, the span is poisoned but for the blocks held
// (memory/poison.hpp): its padding, the space past the cursor and each
// block given back.
//
// thread: one

#include <cstddef>
#include <span>

#include "memory/memory.hpp"

#if defined(QIVEN_SYSTEM_HEAP)
#include <vector>
#endif

namespace qiven3::memory {

class Arena {
public:
    // The caller's pages, which outlive the arena.
    explicit Arena(std::span<std::byte> pages) noexcept;
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = delete;
    Arena& operator=(Arena&&) = delete;
    ~Arena();

    // n bytes (one if n is 0) at a multiple of align, a power of two; past
    // the span, MemoryError budget and nothing changed.
    [[nodiscard]] void* allocate(std::size_t n, std::size_t align);
    // Gives back a block's n bytes to the count. Its bytes stay in the span
    // until the arena's end; under QIVEN_SYSTEM_HEAP the block is freed.
    // A pointer outside the span's front the cursor has passed, or n past
    // it, is a broken invariant.
    void deallocate(void* p, std::size_t n) noexcept;

    // The bytes of the blocks held now.
    [[nodiscard]] std::size_t used() const noexcept { return used_; }
    // The bytes taken from the span, padding included: the high-water mark,
    // since the cursor never goes back.
    [[nodiscard]] std::size_t peak() const noexcept { return cursor_; }
    // The span's size: the operation's budget.
    [[nodiscard]] std::size_t budget() const noexcept { return pages_.size(); }

#if defined(QIVEN_FAULTS)
    // Each allocation asks the countdown first; null detaches it.
    void attach(Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    std::span<std::byte> pages_;
    std::size_t cursor_ = 0;
    std::size_t used_ = 0;
#if defined(QIVEN_SYSTEM_HEAP)
    struct Block {
        void* p = nullptr;
        std::size_t align = 0;
    };
    std::vector<Block> blocks_;
#endif
#if defined(QIVEN_FAULTS)
    Countdown* countdown_ = nullptr;
#endif
};

#if !defined(QIVEN_SYSTEM_HEAP) && !defined(QIVEN_FAULTS)
static_assert(sizeof(Arena) == 32, "Arena's size is pinned: the span, the cursor and the count");
#endif

}  // namespace qiven3::memory
