#pragma once

// served's text, behind served.hpp, which includes it: cost, a text's
// UTF-16 units, and the forms a block writes its values in. A sink is
// Measure, which counts the bytes and units written, each sum checked and
// the bytes bounded by json::max_input (MemoryError budget), or Write, the
// caller's string type written through. put_text escapes a text value onto
// one line: \ LF CR TAB as \\ \n \r \t, the other C0 controls, 0x7f,
// U+0085, U+2028, U+2029 and U+FEFF as \u and four lowercase hex digits,
// each trailing space as \u0020. put_field writes a kind's own field by
// its form: a scalar on its line; a list, its items on theirs; an object,
// its keys on theirs, a list of scalars joined by ", "; a list of objects,
// an item's key=value pairs joined by "; "; a deeper value, its canonical
// JSON, written in its members' order as they stand (the content is
// canonical) with no recursion. Allocations: none. Each is constexpr; it
// reads by index, with no pointer arithmetic.
//
// thread: none

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::served {

// A text's cost: its UTF-16 code units, a four-byte UTF-8 sequence two.
constexpr std::size_t cost(std::string_view text) noexcept {
    std::size_t units = 0;
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if ((u & 0xC0U) != 0x80U) {
            ++units;
        }
        if (u >= 0xF0U) {
            ++units;
        }
    }
    return units;
}

namespace detail {

[[noreturn]] inline void too_large(std::size_t request) {
    throw memory::MemoryError::budget("served", json::max_input, request, 0);
}

// The bytes and UTF-16 units written, each sum checked.
class Measure {
public:
    constexpr void append(std::string_view s) {
        const std::optional<std::size_t> b = memory::add(bytes_, s.size());
        if (!b || *b > json::max_input) {
            too_large(b.value_or(json::max_input + 1));
        }
        bytes_ = *b;
        units_ += cost(s);
    }
    constexpr void push_back(char c) { append(std::string_view(&c, 1)); }
    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr std::size_t units() const noexcept { return units_; }

private:
    std::size_t bytes_ = 0;
    std::size_t units_ = 0;
};

// The caller's string type, written through.
template <typename S>
class Write {
public:
    constexpr explicit Write(S& out) noexcept : out_(&out) {}
    constexpr void append(std::string_view s) { out_->append(s); }
    constexpr void push_back(char c) { out_->push_back(c); }

private:
    S* out_;
};

inline constexpr std::string_view hex = "0123456789abcdef";

template <typename K>
constexpr void put_escape(K& k, unsigned point) {
    k.append("\\u");
    for (unsigned shift = 16; shift > 0;) {
        shift -= 4;
        k.push_back(hex[(point >> shift) & 0xFU]);
    }
}

// A text value, escaped onto one line, its trailing spaces as  .
template <typename K>
constexpr void put_text(K& k, std::string_view s) {
    std::size_t end = s.size();
    while (end > 0 && s[end - 1] == ' ') {
        --end;
    }
    std::size_t run = 0;
    std::size_t i = 0;
    while (i < end) {
        const auto u = static_cast<unsigned char>(s[i]);
        std::string_view short_form;
        unsigned point = 0;
        std::size_t width = 1;
        if (u == '\\') {
            short_form = "\\\\";
        } else if (u == '\n') {
            short_form = "\\n";
        } else if (u == '\r') {
            short_form = "\\r";
        } else if (u == '\t') {
            short_form = "\\t";
        } else if (u < 0x20U || u == 0x7FU) {
            point = u;
        } else if (s.substr(i, 2) == "\xC2\x85") {
            point = 0x85U;
            width = 2;
        } else if (s.substr(i, 3) == "\xE2\x80\xA8" || s.substr(i, 3) == "\xE2\x80\xA9") {
            point = s[i + 2] == '\xA8' ? 0x2028U : 0x2029U;
            width = 3;
        } else if (s.substr(i, 3) == "\xEF\xBB\xBF") {
            point = 0xFEFFU;
            width = 3;
        } else {
            ++i;
            continue;
        }
        k.append(s.substr(run, i - run));
        if (short_form.empty()) {
            put_escape(k, point);
        } else {
            k.append(short_form);
        }
        i += width;
        run = i;
    }
    k.append(s.substr(run, end - run));
    for (std::size_t t = end; t < s.size(); ++t) {
        put_escape(k, 0x20U);
    }
}

template <typename B>
constexpr bool scalar(const canon::Basic<B>& v, json::Index i) {
    const json::Kind kind = v.kind(i);
    return kind != json::Kind::array && kind != json::Kind::object;
}

// A value's canonical JSON, in its members' order as they stand, with no
// recursion: the open levels in a fixed array.
template <typename K, typename B>
constexpr void put_json(K& k, const canon::Basic<B>& v, json::Index root) {
    struct Level {
        json::Index end = 0;
        std::size_t written = 0;
        bool object = false;
    };
    std::array<Level, json::max_depth> levels{};
    std::size_t open = 0;
    const json::Index stop = v.next(root);
    for (json::Index i = root; i < stop; ++i) {
        while (open > 0 && levels.at(open - 1).end == i) {
            --open;
            k.push_back(levels.at(open).object ? '}' : ']');
        }
        if (open > 0) {
            Level& top = levels.at(open - 1);
            if (top.written > 0 && (!top.object || top.written % 2 == 0)) {
                k.push_back(',');
            }
            ++top.written;
            if (top.object && top.written % 2 == 1) {
                json::detail::put_string(k, v.text(i));
                k.push_back(':');
                continue;
            }
        }
        switch (v.kind(i)) {
        case json::Kind::null: k.append("null"); break;
        case json::Kind::boolean: k.append(v.boolean(i) ? "true" : "false"); break;
        case json::Kind::integer: json::detail::put_integer(k, v.integer(i)); break;
        case json::Kind::string: json::detail::put_string(k, v.text(i)); break;
        case json::Kind::array:
        case json::Kind::object:
            k.push_back(v.kind(i) == json::Kind::object ? '{' : '[');
            levels.at(open++) = Level{v.next(i), 0, v.kind(i) == json::Kind::object};
            break;
        }
    }
    while (open > 0) {
        --open;
        k.push_back(levels.at(open).object ? '}' : ']');
    }
}

// A scalar as a value: text escaped, integers in decimal, true, false.
template <typename K, typename B>
constexpr void put_scalar(K& k, const canon::Basic<B>& v, json::Index i) {
    switch (v.kind(i)) {
    case json::Kind::string: put_text(k, v.text(i)); break;
    case json::Kind::integer: json::detail::put_integer(k, v.integer(i)); break;
    case json::Kind::boolean: k.append(v.boolean(i) ? "true" : "false"); break;
    default: put_json(k, v, i); break;
    }
}

// A member's value within an object or a list's item: a scalar, a list of
// scalars joined by sep, or else its canonical JSON.
template <typename K, typename B>
constexpr void put_inner(K& k, const canon::Basic<B>& v, json::Index i, std::string_view sep) {
    if (scalar(v, i)) {
        put_scalar(k, v, i);
        return;
    }
    if (v.kind(i) == json::Kind::array) {
        bool flat = true;
        for (json::Index j = i + 1; j < v.next(i); j = v.next(j)) {
            flat = flat && scalar(v, j);
        }
        if (flat) {
            for (json::Index j = i + 1; j < v.next(i); j = v.next(j)) {
                if (j != i + 1) {
                    k.append(sep);
                }
                put_scalar(k, v, j);
            }
            return;
        }
    }
    put_json(k, v, i);
}

// One field of the kind's own, by its form.
template <typename K, typename B>
constexpr void put_field(K& k, std::string_view field, const canon::Basic<B>& v, json::Index i) {
    k.append(field);
    k.push_back(':');
    if (scalar(v, i)) {
        k.push_back(' ');
        put_scalar(k, v, i);
        k.push_back('\n');
        return;
    }
    k.push_back('\n');
    const json::Index end = v.next(i);
    if (v.kind(i) == json::Kind::object) {
        for (json::Index key = i + 1; key < end; key = v.next(key + 1)) {
            k.append("  ");
            k.append(v.text(key));
            k.append(": ");
            put_inner(k, v, key + 1, ", ");
            k.push_back('\n');
        }
        return;
    }
    for (json::Index item = i + 1; item < end; item = v.next(item)) {
        k.append("  - ");
        if (v.kind(item) == json::Kind::object) {
            for (json::Index key = item + 1; key < v.next(item); key = v.next(key + 1)) {
                if (key != item + 1) {
                    k.append("; ");
                }
                k.append(v.text(key));
                k.push_back('=');
                put_inner(k, v, key + 1, ", ");
            }
        } else {
            put_inner(k, v, item, ", ");
        }
        k.push_back('\n');
    }
}

}  // namespace detail

}  // namespace qiven3::entry::served
