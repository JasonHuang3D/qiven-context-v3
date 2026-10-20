#include "memory/pool.hpp"

#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <optional>

#include "memory/fail.hpp"

// A block is an offset into the span, indexed through it. A pointer given
// back is found in constant time from its offset in the span: placed inside
// the span by std::less first, then subtracted from the span's start, a
// difference of two pointers into one array, the pointer arithmetic memory
// alone may use. std::less is the address order on every target v3 builds
// for, so a pointer it places inside the span is one of the span's.

namespace qiven3::memory {

namespace {

// The span from its first byte at a multiple of slot_align.
std::span<std::byte> aligned(std::span<std::byte> pages) noexcept {
    if (pages.empty()) {
        return pages;
    }
    void* at = pages.data();
    std::size_t space = pages.size();
    if (std::align(slot_align, 1, at, space) == nullptr) {
        return {};
    }
    return pages.subspan(pages.size() - space);
}

#if !defined(QIVEN3_SYSTEM_HEAP)
// p's offset in pages, in constant time; empty if p is not one of its bytes.
std::optional<std::size_t> offset_of(std::span<std::byte> pages, const void* p) noexcept {
    const std::less<const void*> before;
    if (pages.empty() || before(p, pages.data()) || before(&pages.back(), p)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(static_cast<const std::byte*>(p) - pages.data());
}
#endif

std::size_t round_up(std::size_t n) noexcept {
    const std::size_t rounded = add(n, slot_align - 1).value_or(0) / slot_align * slot_align;
    return rounded == 0 ? slot_align : rounded;
}

#if !defined(QIVEN3_SYSTEM_HEAP)
// A heap block's header, before its payload: its class, its state and, when
// free, the next free payload of its class; when held, its own payload's
// offset, which find() checks.
struct Header {
    std::uint32_t cls = 0;
    std::uint32_t state = 0;
    std::size_t next = 0;
};
static_assert(sizeof(Header) == slot_align);
constexpr std::uint32_t held = 0x48454c44U;
constexpr std::uint32_t freed = 0x46524545U;
#endif

constexpr std::size_t payload(std::size_t cls) noexcept { return std::size_t{16} << cls; }
constexpr std::size_t slot_of(std::size_t cls) noexcept { return slot_align + payload(cls); }

std::optional<std::size_t> class_of(std::size_t n) noexcept {
    for (std::size_t c = 0; c < heap_classes; ++c) {
        if (payload(c) >= n) {
            return c;
        }
    }
    return std::nullopt;
}

}  // namespace

// Pool.

Pool::Pool(std::size_t slot, std::span<std::byte> pages) noexcept
    : pages_(aligned(pages)), slot_(round_up(slot)), capacity_(pages_.size() / slot_) {}

void* Pool::allocate() {
#if defined(QIVEN3_FAULTS)
    if (countdown_ != nullptr && countdown_->fail()) {
        throw MemoryError::budget("pool, fault injected", capacity_ * slot_, slot_, used_);
    }
#endif
#if defined(QIVEN3_SYSTEM_HEAP)
    if (free_count_ == 0 && bumped_ == capacity_) {
        throw MemoryError::budget("pool", capacity_ * slot_, slot_, used_);
    }
    void* p = ::operator new(slot_, std::align_val_t{slot_align});
    if (free_count_ > 0) {
        --free_count_;
    } else {
        ++bumped_;
    }
#else
    std::size_t index = free_head_;
    if (index != no_slot) {
        std::memcpy(&free_head_, pages_.subspan(index * slot_, sizeof free_head_).data(),
                    sizeof free_head_);
    } else if (bumped_ < capacity_) {
        index = bumped_;
        ++bumped_;
    } else {
        throw MemoryError::budget("pool", capacity_ * slot_, slot_, used_);
    }
    void* p = &pages_[index * slot_];
#endif
    used_ += slot_;
    if (used_ > peak_) {
        peak_ = used_;
    }
    return p;
}

void Pool::free(void* p) noexcept {
    if (p == nullptr || used_ == 0) {
        fail_fast("pool: a free of a slot it does not hold");
    }
#if defined(QIVEN3_SYSTEM_HEAP)
    ::operator delete(p, std::align_val_t{slot_align});
    ++free_count_;
#else
    // A slot's start, among those handed out: not an interior byte, nor
    // past the last slot cut.
    const std::optional<std::size_t> at = offset_of(pages_, p);
    if (!at || *at % slot_ != 0 || *at / slot_ >= bumped_) {
        fail_fast("pool: a free of a slot it does not hold");
    }
    const std::size_t index = *at / slot_;
    std::memcpy(pages_.subspan(index * slot_, sizeof free_head_).data(), &free_head_,
                sizeof free_head_);
    free_head_ = index;
#endif
    used_ -= slot_;
}

// Heap.

Heap::Heap(std::span<std::byte> pages) noexcept : pages_(aligned(pages)) {}

void Heap::hold() const noexcept {
    if (!mutex_.try_lock_for(heap_wait)) {
        fail_fast("heap: its lock past its deadline");
    }
}

void* Heap::allocate(std::size_t n) noexcept {
    if (!mutex_.try_lock_for(heap_wait)) {
        return nullptr;
    }
    void* p = take(n);
    mutex_.unlock();
    return p;
}

void Heap::free(void* p) noexcept {
    if (p == nullptr) {
        return;
    }
    hold();
    give(p);
    mutex_.unlock();
}

void* Heap::resize(void* p, std::size_t n) noexcept {
    if (p == nullptr) {
        return allocate(n);
    }
    if (!mutex_.try_lock_for(heap_wait)) {
        return nullptr;
    }
    const std::size_t from = find(p).cls;
    const std::optional<std::size_t> to = class_of(n == 0 ? 1 : n);
    void* q = p;
    if (!to) {
        // Past every class: take(n) would refuse it too.
        q = nullptr;
    } else if (*to != from) {
        q = take(n);
        if (q != nullptr) {
            std::memcpy(q, p, payload(from) < payload(*to) ? payload(from) : payload(*to));
            give(p);
        }
    }
    mutex_.unlock();
    return q;
}

std::size_t Heap::size(const void* p) const noexcept {
    if (p == nullptr) {
        return 0;
    }
    hold();
    const std::size_t cls = find(p).cls;
    mutex_.unlock();
    return payload(cls);
}

std::size_t Heap::used() const noexcept {
    hold();
    const std::size_t n = counts_.used;
    mutex_.unlock();
    return n;
}

std::size_t Heap::peak() const noexcept {
    hold();
    const std::size_t n = counts_.peak;
    mutex_.unlock();
    return n;
}

#if defined(QIVEN3_FAULTS)
void Heap::attach(Countdown* countdown) noexcept {
    hold();
    countdown_ = countdown;
    mutex_.unlock();
}
#endif

void* Heap::take(std::size_t n) noexcept {
    const std::optional<std::size_t> cls = class_of(n == 0 ? 1 : n);
    if (!cls) {
        return nullptr;
    }
#if defined(QIVEN3_FAULTS)
    if (countdown_ != nullptr && countdown_->fail()) {
        return nullptr;
    }
#endif
    const std::size_t slot = slot_of(*cls);
    std::size_t& list = free_[*cls];
#if defined(QIVEN3_SYSTEM_HEAP)
    if (list == 0 && slot > pages_.size() - bump_) {
        return nullptr;
    }
    void* p = ::operator new(payload(*cls), std::align_val_t{slot_align}, std::nothrow);
    if (p == nullptr) {
        return nullptr;
    }
    try {
        blocks_.emplace(p, *cls);
    } catch (...) {
        ::operator delete(p, std::align_val_t{slot_align});
        return nullptr;
    }
    if (list > 0) {
        --list;
    } else {
        bump_ += slot;
    }
#else
    std::size_t at = list;
    Header header{};
    if (at != 0) {
        std::memcpy(&header, pages_.subspan(at - slot_align, slot_align).data(), slot_align);
        list = header.next;
    } else if (slot <= pages_.size() - bump_) {
        at = bump_ + slot_align;
        bump_ += slot;
    } else {
        return nullptr;
    }
    header = Header{static_cast<std::uint32_t>(*cls), held, at};
    std::memcpy(pages_.subspan(at - slot_align, slot_align).data(), &header, slot_align);
    void* p = &pages_[at];
#endif
    counts_.used += slot;
    if (counts_.used > counts_.peak) {
        counts_.peak = counts_.used;
    }
    return p;
}

void Heap::give(void* p) noexcept {
    const Block block = find(p);
#if defined(QIVEN3_SYSTEM_HEAP)
    blocks_.erase(p);
    ::operator delete(p, std::align_val_t{slot_align});
    ++free_[block.cls];
#else
    const Header header{static_cast<std::uint32_t>(block.cls), freed, free_[block.cls]};
    std::memcpy(pages_.subspan(block.at - slot_align, slot_align).data(), &header, slot_align);
    free_[block.cls] = block.at;
#endif
    counts_.used -= slot_of(block.cls);
}

Heap::Block Heap::find(const void* p) const noexcept {
#if defined(QIVEN3_SYSTEM_HEAP)
    const auto found = blocks_.find(p);
    if (found == blocks_.end()) {
        fail_fast("heap: a block it does not hold");
    }
    return Block{found->second, 0};
#else
    // A payload follows its header, at a multiple of slot_align within the
    // span's front cut into slots; its header names it held, at this offset.
    const std::optional<std::size_t> at = offset_of(pages_, p);
    if (!at || *at % slot_align != 0 || *at < slot_align || *at >= bump_) {
        fail_fast("heap: a block it does not hold");
    }
    Header header{};
    std::memcpy(&header, pages_.subspan(*at - slot_align, slot_align).data(), slot_align);
    if (header.state != held || header.cls >= heap_classes || header.next != *at) {
        fail_fast("heap: a block it does not hold");
    }
    return Block{header.cls, *at};
#endif
}

}  // namespace qiven3::memory
