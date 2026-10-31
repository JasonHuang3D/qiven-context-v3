#include "memory/arena.hpp"

#include <functional>
#include <memory>
#include <new>

#include "memory/fail.hpp"
#include "memory/poison.hpp"

namespace qiven3::memory {

namespace {

constexpr bool power_of_two(std::size_t n) noexcept { return n != 0 && (n & (n - 1)) == 0; }

}  // namespace

Arena::Arena(std::span<std::byte> pages) noexcept : pages_(pages) { poison(pages_); }

Arena::~Arena() {
#if defined(QIVEN_SYSTEM_HEAP)
    for (const Block& block : blocks_) {
        ::operator delete(block.p, std::align_val_t{block.align});
    }
#endif
    unpoison(pages_);
}

void* Arena::allocate(std::size_t n, std::size_t align) {
    if (!power_of_two(align)) {
        fail_fast("arena: an alignment that is not a power of two");
    }
    const std::size_t bytes = n == 0 ? 1 : n;
#if defined(QIVEN_FAULTS)
    if (countdown_ != nullptr && countdown_->fail()) {
        throw MemoryError::budget("arena, fault injected", pages_.size(), bytes, cursor_);
    }
#endif
    // The cursor's next position: the request at the first multiple of
    // align from the cursor, as the span's own addresses give it.
    if (cursor_ >= pages_.size()) {
        throw MemoryError::budget("arena", pages_.size(), bytes, cursor_);
    }
    void* at = &pages_[cursor_];
    std::size_t space = pages_.size() - cursor_;
    if (std::align(align, bytes, at, space) == nullptr) {
        throw MemoryError::budget("arena", pages_.size(), bytes, cursor_);
    }
    const std::size_t start = pages_.size() - space;
#if defined(QIVEN_SYSTEM_HEAP)
    blocks_.reserve(blocks_.size() + 1);
    at = ::operator new(bytes, std::align_val_t{align});
    blocks_.push_back(Block{at, align});
#else
    unpoison(pages_.subspan(start, bytes));
#endif
    cursor_ = start + bytes;
    used_ += bytes;
    return at;
}

void Arena::deallocate(void* p, std::size_t n) noexcept {
    const std::size_t bytes = n == 0 ? 1 : n;
    if (p == nullptr || bytes > used_) {
        fail_fast("arena: a release of a block it does not hold");
    }
#if defined(QIVEN_SYSTEM_HEAP)
    // The newest block first: containers give back what they took last.
    for (std::size_t i = blocks_.size(); i > 0; --i) {
        const Block block = blocks_[i - 1];
        if (block.p == p) {
            ::operator delete(block.p, std::align_val_t{block.align});
            blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(i - 1));
            used_ -= bytes;
            return;
        }
    }
    fail_fast("arena: a release of a block it does not hold");
#else
    // A block within the span's front the cursor has passed, placed by its
    // offset, a difference of two pointers into the span; then poisoned.
    const std::less<const void*> before;
    if (pages_.empty() || before(p, &pages_.front()) || before(&pages_.back(), p)) {
        fail_fast("arena: a release of a block it does not hold");
    }
    const auto at = static_cast<std::size_t>(static_cast<const std::byte*>(p) - pages_.data());
    if (at >= cursor_ || bytes > cursor_ - at) {
        fail_fast("arena: a release of a block it does not hold");
    }
    poison(pages_.subspan(at, bytes));
    used_ -= bytes;
#endif
}

void* arena_allocate(Arena& arena, std::size_t bytes, std::size_t align) {
    return arena.allocate(bytes, align);
}

void arena_deallocate(Arena& arena, void* p, std::size_t bytes) noexcept {
    arena.deallocate(p, bytes);
}

}  // namespace qiven3::memory
