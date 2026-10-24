#pragma once

// matcher's types: the bounds, the fault, the sets and parts compile
// works in, and the automaton, which hit runs. entry/matcher.hpp states
// the module; include it.
//
// thread: none

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "memory/fail.hpp"
#include "memory/memory.hpp"

namespace qiven3::entry::matcher {

// The bounds: a pattern's bytes, its positions once written out, its open
// groups and a count of {n,m}.
inline constexpr std::size_t max_pattern = 1024;
inline constexpr std::size_t max_positions = 256;
inline constexpr std::size_t max_words = max_positions / 64;
inline constexpr std::size_t max_depth = 8;
inline constexpr std::size_t max_count = 255;

enum class Fault : std::uint8_t { syntax, unsupported, size };

// A pattern compile refuses: the fault and the byte of the pattern at
// which it was found.
struct PatternError {
    Fault code;
    std::size_t at;
};

namespace detail {

// A set of positions, or of byte values: 256 bits either way.
using Set = std::array<std::uint64_t, max_words>;
static_assert(max_words * 64 == 256, "a set of positions and a class of bytes are the same 256 bits");

constexpr void put(Set& s, std::size_t bit) noexcept { s[bit / 64] |= std::uint64_t{1} << (bit % 64); }
constexpr bool has(const Set& s, std::size_t bit) noexcept { return ((s[bit / 64] >> (bit % 64)) & 1U) != 0; }
constexpr void unite(Set& s, const Set& t) noexcept {
    for (std::size_t w = 0; w < max_words; ++w) {
        s[w] |= t[w];
    }
}

// A part of the pattern: its positions [lo, hi), which it alone holds and
// which hold no follower outside it until a later part joins it; where it
// starts and ends; whether it matches the empty string.
struct Fragment {
    std::uint16_t lo = 0;
    std::uint16_t hi = 0;
    Set first{};
    Set last{};
    bool nullable = true;
};

// An open group: its alternatives joined so far, the one being read, and
// its ( for a fault's place.
struct Frame {
    Fragment alt{};
    Fragment cat{};
    bool has_alt = false;
    std::size_t open = 0;
};

// compile's working state, one allocation: each position's followers and
// bytes, and the open groups, the whole pattern's first.
struct Scratch {
    std::array<Set, max_positions> follow{};
    std::array<Set, max_positions> bytes{};
    std::array<Frame, max_depth + 1> frames{};
};

template <typename A>
class Builder;

}  // namespace detail

// An automaton: for each byte value its positions, each position's
// followers, the first and the last positions, W words a set, W the
// positions over 64 rounded up; the lookbehind's class; the mode.
template <typename A = std::allocator<char>>
class Basic {
public:
    using allocator_type = A;
    using Words = std::vector<std::uint64_t, typename std::allocator_traits<A>::template rebind_alloc<std::uint64_t>>;

    constexpr explicit Basic(const A& allocator = A{}) noexcept : words_(allocator) {}

    [[nodiscard]] constexpr A get_allocator() const { return A(words_.get_allocator()); }
    [[nodiscard]] constexpr bool compiled() const noexcept { return width_ != 0; }
    [[nodiscard]] constexpr std::size_t positions() const noexcept { return positions_; }
    [[nodiscard]] constexpr bool whole() const noexcept { return whole_; }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return words_.size() * sizeof(std::uint64_t); }

    // Whether the pattern matches text's bytes, by its mode. An automaton
    // compile did not fill is a caller's mistake: fail_fast.
    [[nodiscard]] constexpr bool hit(std::string_view text) const {
        if (!compiled()) {
            memory::fail_fast("matcher.hit: an automaton compile did not fill");
        }
        if (nullable_ && (!whole_ || text.empty())) {
            return true;
        }
        const std::size_t width = width_;
        const std::size_t follow_at = 256 * width;
        const std::size_t first_at = follow_at + positions_ * width;
        const std::size_t last_at = first_at + width;
        detail::Set current{};
        bool alive = false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            detail::Set next{};
            for (std::size_t w = 0; w < width; ++w) {
                for (std::uint64_t bits = current[w]; bits != 0; bits &= bits - 1) {
                    const std::size_t row = follow_at + (w * 64 + static_cast<std::size_t>(std::countr_zero(bits))) * width;
                    for (std::size_t v = 0; v < width; ++v) {
                        next[v] |= words_[row + v];
                    }
                }
            }
            const bool start =
                whole_ ? i == 0 : i == 0 || !lookbehind_ || !detail::has(behind_, byte(text[i - 1]));
            const std::size_t mask = byte(text[i]) * width;
            alive = false;
            bool done = false;
            for (std::size_t w = 0; w < width; ++w) {
                if (start) {
                    next[w] |= words_[first_at + w];
                }
                next[w] &= words_[mask + w];
                alive = alive || next[w] != 0;
                done = done || (next[w] & words_[last_at + w]) != 0;
            }
            if (whole_ ? !alive : done) {
                return !whole_;
            }
            current = next;
        }
        if (!whole_) {
            return false;
        }
        for (std::size_t w = 0; w < width; ++w) {
            if ((current[w] & words_[last_at + w]) != 0) {
                return true;
            }
        }
        return false;
    }

private:
    template <typename B>
    friend class detail::Builder;

    static constexpr std::size_t byte(char c) noexcept { return static_cast<unsigned char>(c); }

    Words words_;
    detail::Set behind_{};
    std::uint16_t positions_ = 0;
    std::uint8_t width_ = 0;
    bool whole_ = false;
    bool nullable_ = false;
    bool lookbehind_ = false;
};

using Automaton = Basic<memory::Allocator<char>>;

#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Automaton) == 72, "the automaton's size is pinned: its words, 32, the lookbehind, 32, the rest");
#endif

// An automaton's bytes for its positions: 256 masks, a row each, first
// and last, each of W words.
constexpr std::size_t automaton_bytes(std::size_t positions) noexcept {
    const std::size_t width = positions == 0 ? 1 : (positions + 63) / 64;
    return (256 + positions + 2) * width * sizeof(std::uint64_t);
}

// compile's peak: its scratch and the largest automaton.
inline constexpr std::size_t compile_budget = sizeof(detail::Scratch) + automaton_bytes(max_positions);

}  // namespace qiven3::entry::matcher
