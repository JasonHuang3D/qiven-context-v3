#pragma once

// poison: AddressSanitizer's view of the bytes an arena, a pool or a heap
// holds. Under AddressSanitizer, with the blocks in the span (no
// QIVEN3_SYSTEM_HEAP), each allocator poisons what it holds and has not
// handed out: the span at its start, a freed slot, a heap block's header,
// an arena's space past its cursor and its padding; it unpoisons exactly
// what it hands out, and its whole span at its end, when the pages go back
// to their owner. A touch of a poisoned byte is the sanitizer's report.
// Elsewhere the two calls are empty.
//
// The sanitizer marks bytes in granules of 8: a block that starts inside a
// granule makes the bytes before it in that granule addressable too, and a
// poisoned range never covers a byte past it that is addressable.
//
// thread: any; each call marks only the bytes it names.

#include <cstddef>
#include <span>

#if defined(__SANITIZE_ADDRESS__)
#define QIVEN3_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define QIVEN3_ASAN 1
#endif
#endif

#if defined(QIVEN3_ASAN) && !defined(QIVEN3_SYSTEM_HEAP)
#define QIVEN3_POISONING 1
#include <sanitizer/asan_interface.h>
#define QIVEN3_POISON(p, n) ASAN_POISON_MEMORY_REGION((p), (n))
#define QIVEN3_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#define QIVEN3_POISONING 0
#define QIVEN3_POISON(p, n)
#define QIVEN3_UNPOISON(p, n)
#endif

namespace qiven3::memory {

// Whether the allocators poison what they hold, in this build.
inline constexpr bool poisoning = QIVEN3_POISONING != 0;

// Marks bytes the allocator holds and has not handed out.
inline void poison([[maybe_unused]] std::span<std::byte> bytes) noexcept {
    QIVEN3_POISON(bytes.data(), bytes.size());
}

// Marks bytes it hands out, or gives back to their owner.
inline void unpoison([[maybe_unused]] std::span<std::byte> bytes) noexcept {
    QIVEN3_UNPOISON(bytes.data(), bytes.size());
}

}  // namespace qiven3::memory
