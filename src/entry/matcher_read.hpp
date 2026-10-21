#pragma once

// matcher's reader of a pattern: compile's builder, which reads the
// pattern into its scratch in one pass and writes the automaton in one
// allocation. entry/matcher.hpp states the module; include it.
//
// thread: none

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/matcher_basic.hpp"
#include "memory/memory.hpp"

namespace qiven3::entry::matcher {

namespace detail {

template <typename A>
class Builder {
public:
    using Rebound = typename std::allocator_traits<A>::template rebind_alloc<Scratch>;

    // The scratch, allocated here, value-initialized.
    constexpr explicit Builder(const A& allocator) : scratch_(1, Rebound(allocator)) {}

    // Reads the pattern; the first fault, or none.
    constexpr std::optional<PatternError> read(std::string_view p) noexcept {
        if (p.size() > max_pattern) {
            return PatternError{Fault::size, max_pattern};
        }
        std::size_t i = 0;
        if (!p.empty() && p[0] == '^') {
            whole_ = true;
            i = 1;
        }
        if (p.substr(i).starts_with("(?<!")) {
            i += 4;
            if (whole_ || i >= p.size() || p[i] != '[') {
                return fail(Fault::unsupported, i);
            }
            if (!klass(p, i, behind_)) {
                return fault_;
            }
            if (i >= p.size() || p[i] != ')') {
                return fail(Fault::unsupported, i);
            }
            ++i;
            lookbehind_ = true;
        }
        s().frames[0] = Frame{};
        bool closed = false;
        while (i < p.size() && !fault_) {
            if (closed) {
                return fail(Fault::unsupported, i);
            }
            step(p, i, closed);
        }
        if (fault_) {
            return fault_;
        }
        if (depth_ != 0) {
            return fail(Fault::syntax, s().frames[depth_].open);
        }
        if (whole_ && !closed) {
            return fail(Fault::unsupported, p.size());
        }
        root_ = close(s().frames[0]);
        return std::nullopt;
    }

    // The automaton of a pattern read: one allocation.
    constexpr void write(Basic<A>& out) const {
        const std::size_t width = n_ == 0 ? 1 : (n_ + 63) / 64;
        typename Basic<A>::Words words(automaton_bytes(n_) / sizeof(std::uint64_t), std::uint64_t{0}, out.words_.get_allocator());
        for (std::size_t p = 0; p < n_; ++p) {
            for (std::size_t w = 0; w < max_words; ++w) {
                for (std::uint64_t bits = s().bytes[p][w]; bits != 0; bits &= bits - 1) {
                    const std::size_t b = w * 64 + static_cast<std::size_t>(std::countr_zero(bits));
                    words[b * width + p / 64] |= std::uint64_t{1} << (p % 64);
                }
            }
            for (std::size_t w = 0; w < width; ++w) {
                words[(256 + p) * width + w] = s().follow[p][w];
            }
        }
        for (std::size_t w = 0; w < width; ++w) {
            words[(256 + n_) * width + w] = root_.first[w];
            words[(257 + n_) * width + w] = root_.last[w];
        }
        out.words_ = std::move(words);
        out.behind_ = behind_;
        out.positions_ = static_cast<std::uint16_t>(n_);
        out.width_ = static_cast<std::uint8_t>(width);
        out.whole_ = whole_;
        out.nullable_ = root_.nullable;
        out.lookbehind_ = lookbehind_;
    }

private:
    constexpr std::optional<PatternError> fail(Fault code, std::size_t at) noexcept {
        if (!fault_) {
            fault_ = PatternError{code, at};
        }
        return fault_;
    }

    constexpr bool refuse(Fault code, std::size_t at) noexcept {
        fail(code, at);
        return false;
    }

    static constexpr bool printable(char c) noexcept { return c >= 0x20 && c <= 0x7e; }
    static constexpr bool alnum(char c) noexcept {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    // One token at i, which it passes.
    constexpr void step(std::string_view p, std::size_t& i, bool& closed) noexcept {
        const char c = p[i];
        Frame& top = s().frames[depth_];
        if (c == '(') {
            if (depth_ == max_depth) {
                fail(Fault::size, i);
                return;
            }
            const std::size_t open = i;
            if (p.substr(i).starts_with("(?:")) {
                i += 3;
            } else if (p.substr(i).starts_with("(?")) {
                fail(Fault::unsupported, i);
                return;
            } else {
                ++i;
            }
            ++depth_;
            s().frames[depth_] = Frame{.alt = empty(), .cat = empty(), .has_alt = false, .open = open};
        } else if (c == ')') {
            if (depth_ == 0) {
                fail(Fault::syntax, i);
                return;
            }
            Fragment group = close(top);
            --depth_;
            ++i;
            if (quantify(p, group, i)) {
                s().frames[depth_].cat = concat(s().frames[depth_].cat, group);
            }
        } else if (c == '|') {
            top.alt = top.has_alt ? alt(top.alt, top.cat) : top.cat;
            top.has_alt = true;
            top.cat = empty();
            ++i;
        } else if (c == '$') {
            if (!whole_ || i + 1 != p.size()) {
                fail(Fault::unsupported, i);
                return;
            }
            closed = true;
            ++i;
        } else if (c == '*' || c == '+' || c == '?' || c == '{') {
            fail(Fault::syntax, i);
        } else if (c == '.' || c == '^' || !printable(c)) {
            fail(Fault::unsupported, i);
        } else {
            Set bytes{};
            if (c == '[' ? klass(p, i, bytes) : single(p, i, bytes)) {
                atom(p, bytes, i);
            }
        }
    }

    // One character at i, a literal or an escape, which it passes.
    constexpr bool character(std::string_view p, std::size_t& i, std::size_t& out) noexcept {
        const char c = p[i];
        if (c != '\\') {
            if (!printable(c)) {
                fail(Fault::unsupported, i);
                return false;
            }
            out = static_cast<unsigned char>(c);
            ++i;
            return true;
        }
        if (i + 1 >= p.size()) {
            fail(Fault::syntax, i);
            return false;
        }
        const char e = p[i + 1];
        if (e == 't' || e == 'n' || e == 'r') {
            out = e == 't' ? std::size_t{0x09} : e == 'n' ? std::size_t{0x0a} : std::size_t{0x0d};
        } else if (printable(e) && !alnum(e)) {
            out = static_cast<unsigned char>(e);
        } else {
            fail(Fault::unsupported, i);
            return false;
        }
        i += 2;
        return true;
    }

    constexpr bool single(std::string_view p, std::size_t& i, Set& out) noexcept {
        std::size_t b = 0;
        if (!character(p, i, b)) {
            return false;
        }
        put(out, b);
        return true;
    }

    // A bracket class at i, which it passes.
    constexpr bool klass(std::string_view p, std::size_t& i, Set& out) noexcept {
        const std::size_t open = i;
        ++i;
        const bool negate = i < p.size() && p[i] == '^';
        if (negate) {
            ++i;
        }
        for (bool first = true;; first = false) {
            if (i >= p.size()) {
                fail(Fault::syntax, open);
                return false;
            }
            if (p[i] == ']' && !first) {
                ++i;
                break;
            }
            if (p[i] == '[') {
                fail(Fault::unsupported, i);
                return false;
            }
            std::size_t lo = 0;
            if (!character(p, i, lo)) {
                return false;
            }
            std::size_t hi = lo;
            if (i + 1 < p.size() && p[i] == '-' && p[i + 1] != ']') {
                const std::size_t dash = i;
                ++i;
                if (p[i] == '[') {
                    fail(Fault::unsupported, i);
                    return false;
                }
                if (!character(p, i, hi)) {
                    return false;
                }
                if (hi < lo) {
                    fail(Fault::syntax, dash);
                    return false;
                }
            }
            for (std::size_t b = lo; b <= hi; ++b) {
                put(out, b);
            }
        }
        if (negate) {
            for (std::uint64_t& w : out) {
                w = ~w;
            }
        }
        return true;
    }

    constexpr Fragment empty() const noexcept {
        Fragment f;
        f.lo = static_cast<std::uint16_t>(n_);
        f.hi = f.lo;
        return f;
    }

    // A position of one class, quantified, joined to the open group.
    constexpr void atom(std::string_view p, const Set& bytes, std::size_t& i) noexcept {
        if (n_ == max_positions) {
            fail(Fault::size, i);
            return;
        }
        s().bytes[n_] = bytes;
        s().follow[n_] = Set{};
        Fragment f = empty();
        put(f.first, n_);
        put(f.last, n_);
        f.nullable = false;
        ++n_;
        f.hi = static_cast<std::uint16_t>(n_);
        if (quantify(p, f, i)) {
            s().frames[depth_].cat = concat(s().frames[depth_].cat, f);
        }
    }

    // A count at i, {n}, {n,} or {n,m}, which it passes.
    constexpr bool count(std::string_view p, std::size_t& i, std::size_t& at, bool& digits) noexcept {
        at = 0;
        digits = false;
        for (; i < p.size() && p[i] >= '0' && p[i] <= '9'; ++i) {
            digits = true;
            at = at > max_count ? at : at * 10 + (static_cast<std::size_t>(p[i]) - static_cast<std::size_t>('0'));
        }
        return at <= max_count;
    }

    // The quantifier at i on f, the newest part, if one is there.
    constexpr bool quantify(std::string_view p, Fragment& f, std::size_t& i) noexcept {
        if (i >= p.size()) {
            return true;
        }
        const std::size_t at = i;
        std::size_t lo = 0;
        std::size_t hi = 1;
        bool unbounded = false;
        const char c = p[i];
        if (c == '*' || c == '+') {
            lo = c == '+' ? 1 : 0;
            unbounded = true;
            ++i;
        } else if (c == '?') {
            ++i;
        } else if (c == '{') {
            ++i;
            bool digits = false;
            if (!count(p, i, lo, digits)) {
                return refuse(Fault::size, at);
            }
            if (!digits) {
                return refuse(Fault::unsupported, at);
            }
            hi = lo;
            if (i < p.size() && p[i] == ',') {
                ++i;
                bool more = false;
                if (!count(p, i, hi, more)) {
                    return refuse(Fault::size, at);
                }
                unbounded = !more;
            }
            if (i >= p.size() || p[i] != '}') {
                return refuse(Fault::unsupported, at);
            }
            ++i;
            if (!unbounded && hi < lo) {
                return refuse(Fault::syntax, at);
            }
        } else {
            return true;
        }
        if (i < p.size() && (p[i] == '?' || p[i] == '+')) {
            return refuse(Fault::unsupported, i);
        }
        if (i < p.size() && (p[i] == '*' || p[i] == '{')) {
            return refuse(Fault::syntax, i);
        }
        return repeat(f, lo, hi, unbounded, at);
    }

    // f, lo to hi times or lo and more: written out in copies of f, the
    // first lo of them required, the rest optional; with no bound the last
    // copy loops.
    constexpr bool repeat(Fragment& f, std::size_t lo, std::size_t hi, bool unbounded, std::size_t at) noexcept {
        const std::size_t length = static_cast<std::size_t>(f.hi) - static_cast<std::size_t>(f.lo);
        const std::size_t copies = unbounded ? (lo == 0 ? 1 : lo) : hi;
        const std::optional<std::size_t> extra = memory::mul(copies == 0 ? 0 : copies - 1, length);
        const std::optional<std::size_t> total = extra ? memory::add(n_, *extra) : std::nullopt;
        if (!total || *total > max_positions) {
            return refuse(Fault::size, at);
        }
        if (copies == 0) {
            f.first = Set{};
            f.last = Set{};
            f.nullable = true;
            return true;
        }
        const Fragment pristine = f;
        Fragment whole = f;
        for (std::size_t j = 0; j < copies; ++j) {
            Fragment part = j == 0 ? pristine : copy(pristine, j * length);
            if (unbounded && j + 1 == copies) {
                loop(part);
            }
            part.nullable = part.nullable || j >= lo;
            whole = j == 0 ? part : concat(whole, part);
        }
        n_ = *total;
        f = whole;
        return true;
    }

    // f's positions again, by offset past them; its followers inside it.
    constexpr Fragment copy(const Fragment& f, std::size_t offset) noexcept {
        Fragment out;
        out.lo = static_cast<std::uint16_t>(f.lo + offset);
        out.hi = static_cast<std::uint16_t>(f.hi + offset);
        out.nullable = f.nullable;
        for (std::size_t p = f.lo; p < f.hi; ++p) {
            s().bytes[p + offset] = s().bytes[p];
            s().follow[p + offset] = Set{};
            for (std::size_t q = f.lo; q < f.hi; ++q) {
                if (has(s().follow[p], q)) {
                    put(s().follow[p + offset], q + offset);
                }
            }
            if (has(f.first, p)) {
                put(out.first, p + offset);
            }
            if (has(f.last, p)) {
                put(out.last, p + offset);
            }
        }
        return out;
    }

    // Each last position of from followed by each of to.
    constexpr void follow(const Fragment& from, const Set& to) noexcept {
        for (std::size_t w = 0; w < max_words; ++w) {
            for (std::uint64_t bits = from.last[w]; bits != 0; bits &= bits - 1) {
                unite(s().follow[w * 64 + static_cast<std::size_t>(std::countr_zero(bits))], to);
            }
        }
    }

    constexpr void loop(const Fragment& f) noexcept { follow(f, f.first); }

    constexpr Fragment concat(const Fragment& a, const Fragment& b) noexcept {
        follow(a, b.first);
        Fragment out = a;
        out.hi = b.hi;
        if (a.nullable) {
            unite(out.first, b.first);
        }
        out.last = b.last;
        if (b.nullable) {
            unite(out.last, a.last);
        }
        out.nullable = a.nullable && b.nullable;
        return out;
    }

    static constexpr Fragment alt(const Fragment& a, const Fragment& b) noexcept {
        Fragment out = a;
        out.hi = b.hi;
        unite(out.first, b.first);
        unite(out.last, b.last);
        out.nullable = a.nullable || b.nullable;
        return out;
    }

    static constexpr Fragment close(const Frame& frame) noexcept {
        return frame.has_alt ? alt(frame.alt, frame.cat) : frame.cat;
    }

    constexpr Scratch& s() noexcept { return scratch_[0]; }
    [[nodiscard]] constexpr const Scratch& s() const noexcept { return scratch_[0]; }

    std::vector<Scratch, Rebound> scratch_;
    std::optional<PatternError> fault_;
    Fragment root_{};
    Set behind_{};
    std::size_t n_ = 0;
    std::size_t depth_ = 0;
    bool whole_ = false;
    bool lookbehind_ = false;
};

}  // namespace detail

}  // namespace qiven3::entry::matcher
