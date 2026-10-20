#pragma once

// memory: v3's allocator type and containers over an arena, the budget's
// error, bounded text, the lock, checked sizes and, under QIVEN3_FAULTS, the
// failing countdown. Ownership is single and scoped: an arena lends its
// bytes to the containers of one operation and takes them all back at its
// end.
//
// thread: one, but Mutex, which is shared by the threads that lock it.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// The thread-safety attributes, read by clang's -Wthread-safety and empty
// for gcc and MSVC.
#if defined(__clang__)
#define QIVEN3_THREAD_ATTRIBUTE(x) __attribute__((x))
#else
#define QIVEN3_THREAD_ATTRIBUTE(x)
#endif
#define QIVEN3_CAPABILITY(x) QIVEN3_THREAD_ATTRIBUTE(capability(x))
#define QIVEN3_GUARDED_BY(x) QIVEN3_THREAD_ATTRIBUTE(guarded_by(x))
#define QIVEN3_REQUIRES(...) QIVEN3_THREAD_ATTRIBUTE(requires_capability(__VA_ARGS__))
#define QIVEN3_ACQUIRE(...) QIVEN3_THREAD_ATTRIBUTE(acquire_capability(__VA_ARGS__))
#define QIVEN3_TRY_ACQUIRE(...) QIVEN3_THREAD_ATTRIBUTE(try_acquire_capability(__VA_ARGS__))
#define QIVEN3_RELEASE(...) QIVEN3_THREAD_ATTRIBUTE(release_capability(__VA_ARGS__))

// A stateless member takes no space: MSVC ignores the standard spelling.
#if defined(_MSC_VER)
#define QIVEN3_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define QIVEN3_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace qiven3::memory {

class Arena;

// Checked size arithmetic: empty on overflow.
constexpr std::optional<std::size_t> add(std::size_t a, std::size_t b) noexcept {
    if (a > std::numeric_limits<std::size_t>::max() - b) {
        return std::nullopt;
    }
    return a + b;
}

constexpr std::optional<std::size_t> mul(std::size_t a, std::size_t b) noexcept {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return std::nullopt;
    }
    return a * b;
}

// At most N bytes of text held in place, cut at a code point: a cut never
// leaves part of a UTF-8 sequence.
template <std::size_t N>
class Inline {
public:
    constexpr Inline() noexcept = default;
    constexpr explicit Inline(std::string_view text) noexcept {
        std::size_t cut = text.size() < N ? text.size() : N;
        if (cut < text.size()) {
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
                --cut;
            }
        }
        for (std::size_t i = 0; i < cut; ++i) {
            bytes_[i] = text[i];
        }
        size_ = cut;
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(bytes_.data(), size_);
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

private:
    std::size_t size_ = 0;
    std::array<char, N> bytes_{};
};

enum class MemoryCode : std::uint8_t { budget };

std::string_view name(MemoryCode code) noexcept;

// The budget's error: a request past an arena's or a pool's span. A plain
// value, thrown by value; it allocates nothing.
struct MemoryError {
    MemoryError(MemoryCode code_, std::string_view operation_, std::string_view detail_) noexcept
        : code(code_), operation(operation_), detail(detail_) {}
    // "budget B, request R, held H", each a count of bytes.
    static MemoryError budget(std::string_view operation, std::size_t budget, std::size_t request,
                              std::size_t held) noexcept;

    MemoryCode code;
    Inline<32> operation;
    Inline<96> detail;
};

#if defined(QIVEN3_FAULTS)
// The failing countdown: armed at k, the k-th call of fail() after arming
// returns true, the k-th allocation (or platform call) failing; every other
// call returns false. Unarmed, none fails. thread: one.
class Countdown {
public:
    void arm(std::size_t k) noexcept {
        target_ = k;
        calls_ = 0;
        fired_ = false;
    }
    void disarm() noexcept { arm(0); }
    [[nodiscard]] bool fail() noexcept {
        ++calls_;
        if (target_ != 0 && calls_ == target_) {
            fired_ = true;
            return true;
        }
        return false;
    }
    // The calls since arming, and whether the armed one came.
    [[nodiscard]] std::size_t calls() const noexcept { return calls_; }
    [[nodiscard]] bool fired() const noexcept { return fired_; }

private:
    std::size_t target_ = 0;
    std::size_t calls_ = 0;
    bool fired_ = false;
};
#endif

// The arena's half of Allocator<T>, defined by the arena module.
[[nodiscard]] void* arena_allocate(Arena& arena, std::size_t bytes, std::size_t align);
void arena_deallocate(Arena& arena, void* p, std::size_t bytes) noexcept;

// The allocator of v3's containers: one Arena*. Copies compare equal for
// one arena and propagate with the container, so a moved container keeps
// its arena. A request past the arena's budget throws MemoryError budget.
template <typename T>
class Allocator {
public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::false_type;

    explicit Allocator(Arena& arena) noexcept : arena_(&arena) {}
    template <typename U>
    Allocator(const Allocator<U>& other) noexcept : arena_(&other.arena()) {}

    [[nodiscard]] T* allocate(std::size_t n) {
        // An overflowing count asks for every byte, which no arena holds.
        const std::size_t bytes =
            mul(n, sizeof(T)).value_or(std::numeric_limits<std::size_t>::max());
        return static_cast<T*>(arena_allocate(*arena_, bytes, alignof(T)));
    }
    void deallocate(T* p, std::size_t n) noexcept {
        // n * sizeof(T) did not overflow when these were allocated.
        arena_deallocate(*arena_, p, n * sizeof(T));
    }
    [[nodiscard]] Arena& arena() const noexcept { return *arena_; }

    template <typename U>
    friend bool operator==(const Allocator& a, const Allocator<U>& b) noexcept {
        return &a.arena() == &b.arena();
    }

private:
    Arena* arena_;
};

using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;
template <typename T>
using Vector = std::vector<T, Allocator<T>>;

// The pinned sizes, on x64 with MSVC's library; its debug iterators, on in
// Debug, add a pointer to each container.
static_assert(sizeof(Allocator<char>) == 8, "Allocator<T>'s size is pinned: one Arena*");
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(String) == 40, "String's size is pinned");
static_assert(sizeof(Vector<int>) == 32, "Vector<T>'s size is pinned");
#endif

// The one lock of shared state: a std::timed_mutex locked by try_lock_for
// alone, so every wait has a deadline. thread: shared.
class QIVEN3_CAPABILITY("mutex") Mutex {
public:
    Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    ~Mutex() = default;

    template <typename Rep, typename Period>
    [[nodiscard]] bool try_lock_for(std::chrono::duration<Rep, Period> wait) QIVEN3_TRY_ACQUIRE(true) {
        return mutex_.try_lock_for(wait);
    }
    void unlock() QIVEN3_RELEASE() { mutex_.unlock(); }

private:
    std::timed_mutex mutex_;
};

}  // namespace qiven3::memory
