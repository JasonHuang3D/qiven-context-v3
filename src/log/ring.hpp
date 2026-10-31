#pragma once

// ring: the crash buffer, whole lines over a span its Log takes once from
// its binary's arena. A line goes in with its '\n' and never wraps: when the
// span's end is near, the next line starts at its front, and the oldest
// lines are evicted whole, first, until it fits. The held lines are so at
// most two pieces of whole lines, the older first.
//
// The ring's state, the first held line's offset, the end of the newest and
// where the older piece ends, is one 64-bit atomic. A push publishes the
// evictions by a release store before it writes a byte, and the new line
// by another after; lines() reads the state by one acquire load, with no
// lock and no call that can fail, so that the diagnostic entry's report
// may read it from a handler. Of what the writer has published, the reader
// sees whole lines alone.
//
// thread: one writer, the Log's thread; lines() from any thread or a
// signal handler, which sees what the last release store committed.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>

namespace qiven3::log {

// The held lines, the older piece first, each piece whole lines ending in
// '\n'. Borrowed from the ring: valid until the ring's next push.
struct RingView {
    std::string_view older;
    std::string_view newer;
};

class Ring {
public:
    // The largest span a ring takes: its offsets are 21 bits of its state.
    static constexpr std::size_t max_bytes = std::size_t{1} << 20U;

    // The span outlives the ring. Past max_bytes, or under 2 bytes, is a
    // broken invariant.
    explicit Ring(std::span<char> storage) noexcept;
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;
    Ring(Ring&&) = delete;
    Ring& operator=(Ring&&) = delete;
    ~Ring() = default;

    // Appends line and its '\n', evicting the oldest lines whole. A line
    // holding '\n', or one that with its '\n' takes the whole span, is not
    // taken: false, nothing changed.
    bool push(std::string_view line) noexcept;

    // The held lines, by one acquire load. Inline, with no checked member,
    // for the diagnostic entry's handlers.
    [[nodiscard]] RingView lines() const noexcept { return view(state_.load(std::memory_order_acquire)); }
    // The held lines from the seq-th pushed on (from the first held when seq
    // is before it): the writer's thread alone.
    [[nodiscard]] RingView since(std::uint64_t seq) const noexcept;

    // The number of the oldest line held, counted from 0 since the ring
    // began: the lines evicted so far. The writer's thread alone.
    [[nodiscard]] std::uint64_t first() const noexcept { return first_; }
    // The number the next line pushed takes. The writer's thread alone.
    [[nodiscard]] std::uint64_t next() const noexcept { return next_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }

private:
    struct State {
        std::size_t head = 0;  // the oldest line's offset
        std::size_t tail = 0;  // past the newest line's '\n'
        std::size_t wrap = 0;  // past the older piece's last line; 0 when one piece
        // One piece: [head, tail), empty when they meet. Two: [head, wrap)
        // then [0, tail), tail at most head.
    };
    static constexpr unsigned bits = 21;
    static constexpr std::uint64_t mask = (std::uint64_t{1} << bits) - 1U;

    static constexpr std::uint64_t pack(const State& s) noexcept {
        return static_cast<std::uint64_t>(s.head) | (static_cast<std::uint64_t>(s.tail) << bits) |
               (static_cast<std::uint64_t>(s.wrap) << (2U * bits));
    }
    static constexpr State unpack(std::uint64_t word) noexcept {
        return State{static_cast<std::size_t>(word & mask), static_cast<std::size_t>((word >> bits) & mask),
                     static_cast<std::size_t>((word >> (2U * bits)) & mask)};
    }
    [[nodiscard]] std::string_view piece(std::size_t from, std::size_t to) const noexcept {
        return {std::next(storage_.begin(), static_cast<std::ptrdiff_t>(from)),
                std::next(storage_.begin(), static_cast<std::ptrdiff_t>(to))};
    }
    [[nodiscard]] RingView view(std::uint64_t word) const noexcept {
        const State s = unpack(word);
        if (s.wrap == 0) {
            return RingView{piece(s.head, s.tail), {}};
        }
        return RingView{piece(s.head, s.wrap), piece(0, s.tail)};
    }
    // The oldest line of s evicted: head past its '\n'.
    void evict(State& s) const noexcept;
    void write_at(std::size_t at, std::string_view line) noexcept;

    std::span<char> storage_;
    std::atomic<std::uint64_t> state_{0};
    std::uint64_t first_ = 0;
    std::uint64_t next_ = 0;
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "the ring's state is read from a signal handler");
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(Ring) == 40, "Ring's size is pinned: the span, the state and the two counts");
#endif

}  // namespace qiven3::log
