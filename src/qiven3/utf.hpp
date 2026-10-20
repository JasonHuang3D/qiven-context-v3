#pragma once

// utf: strict UTF-8, code points and UTF-16 units, code point order, the
// text's fixed code point ranges, and a system name's lone surrogates
// written as Python's backslashreplace writes them. Every function is pure
// and constexpr, a template on the container it writes into, so its vectors
// run in constant evaluation too; it reads by index, with no pointer
// arithmetic. The parser's typed error, ParseError, is declared here, the
// lowest module that throws it; json and canon throw it too.
//
// thread: none

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "memory/fail.hpp"

namespace qiven3 {

// The strict parser's codes, in the order of its steps.
enum class ParseCode : std::uint8_t {
    bom,
    utf8,
    depth,
    syntax,
    floating,
    constant,
    int_range,
    duplicate_key,
    null,
    surrogate,
    nonchar,
    key_syntax,
    not_canonical,
};

// The code's name as the text writes it: "float" for floating.
std::string_view name(ParseCode code) noexcept;

// A fault in the input: a plain value, thrown by value; it allocates
// nothing.
struct ParseError {
    ParseCode code;
};

namespace utf {

// What a text is: bytes from outside (strict: an encoded surrogate is a
// fault in UTF-8), or a parsed value's text, where the parser keeps a lone
// \u surrogate escape in its three-byte form for the walk to refuse.
enum class Form : std::uint8_t { strict, value };

// One code point read: its value and its bytes, or no bytes for a fault.
struct Read {
    char32_t point = 0;
    std::size_t size = 0;
    bool surrogate = false;
};

constexpr bool is_surrogate(char32_t point) noexcept { return point >= 0xD800 && point <= 0xDFFF; }

// The 66 noncharacters: U+FDD0 to U+FDEF and each plane's last two.
constexpr bool is_nonchar(char32_t point) noexcept {
    return (point >= 0xFDD0 && point <= 0xFDEF) || (point & 0xFFFEU) == 0xFFFEU;
}

constexpr unsigned byte(std::string_view text, std::size_t at) noexcept {
    return static_cast<unsigned char>(text[at]);
}

// The code point at text[at], at < text.size(): RFC 3629's well-formed
// sequences, in the value form the surrogates' three-byte form too.
constexpr Read next(std::string_view text, std::size_t at, Form form = Form::strict) noexcept {
    const unsigned b0 = byte(text, at);
    if (b0 < 0x80U) {
        return Read{b0, 1, false};
    }
    std::size_t need = 0;
    char32_t point = 0;
    unsigned lo = 0x80U;
    unsigned hi = 0xBFU;
    if (b0 < 0xC2U) {
        return Read{};  // a continuation byte, or an overlong C0 or C1
    }
    if (b0 < 0xE0U) {
        need = 2;
        point = b0 & 0x1FU;
    } else if (b0 < 0xF0U) {
        need = 3;
        point = b0 & 0x0FU;
        if (b0 == 0xE0U) {
            lo = 0xA0U;
        } else if (b0 == 0xEDU && form == Form::strict) {
            hi = 0x9FU;
        }
    } else if (b0 < 0xF5U) {
        need = 4;
        point = b0 & 0x07U;
        if (b0 == 0xF0U) {
            lo = 0x90U;
        } else if (b0 == 0xF4U) {
            hi = 0x8FU;
        }
    } else {
        return Read{};  // past U+10FFFF
    }
    if (text.size() - at < need) {
        return Read{};  // truncated
    }
    for (std::size_t i = 1; i < need; ++i) {
        const unsigned b = byte(text, at + i);
        if (b < lo || b > hi) {
            return Read{};
        }
        lo = 0x80U;
        hi = 0xBFU;
        point = (point << 6U) | (b & 0x3FU);
    }
    return Read{point, need, is_surrogate(point)};
}

// The first fault in document order, or none: utf8, or in the value form
// surrogate for a lone surrogate's three-byte form.
constexpr std::optional<ParseCode> check(std::string_view text, Form form = Form::strict) noexcept {
    for (std::size_t at = 0; at < text.size();) {
        const Read r = next(text, at, form);
        if (r.size == 0) {
            return ParseCode::utf8;
        }
        if (r.surrogate) {
            return ParseCode::surrogate;
        }
        at += r.size;
    }
    return std::nullopt;
}

// A valid text's length in code points: its bytes that are no continuation.
constexpr std::size_t length(std::string_view text) noexcept {
    std::size_t n = 0;
    for (std::size_t at = 0; at < text.size(); ++at) {
        n += (byte(text, at) & 0xC0U) != 0x80U ? 1U : 0U;
    }
    return n;
}

// A valid text's UTF-16 code units: its code points, plus one for each past
// U+FFFF, a four-byte sequence.
constexpr std::size_t units16(std::string_view text) noexcept {
    std::size_t n = length(text);
    for (std::size_t at = 0; at < text.size(); ++at) {
        n += byte(text, at) >= 0xF0U ? 1U : 0U;
    }
    return n;
}

// Code point order, which is UTF-8 byte order, each byte unsigned.
constexpr std::strong_ordering compare(std::string_view a, std::string_view b) noexcept {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (byte(a, i) != byte(b, i)) {
            return byte(a, i) <=> byte(b, i);
        }
    }
    return a.size() <=> b.size();
}

// The UTF-8 of a code point at most U+10FFFF, a surrogate in its
// three-byte form, appended to a string type's out.
template <typename S>
constexpr void append(S& out, char32_t point) {
    const auto put = [&out](char32_t bits) { out.push_back(static_cast<char>(static_cast<unsigned char>(bits))); };
    if (point < 0x80U) {
        put(point);
    } else if (point < 0x800U) {
        put(0xC0U | (point >> 6U));
        put(0x80U | (point & 0x3FU));
    } else if (point < 0x10000U) {
        put(0xE0U | (point >> 12U));
        put(0x80U | ((point >> 6U) & 0x3FU));
        put(0x80U | (point & 0x3FU));
    } else if (point <= 0x10FFFFU) {
        put(0xF0U | (point >> 18U));
        put(0x80U | ((point >> 12U) & 0x3FU));
        put(0x80U | ((point >> 6U) & 0x3FU));
        put(0x80U | (point & 0x3FU));
    } else {
        memory::fail_fast("utf::append: a code point past U+10FFFF");
    }
}

// The text's code points appended to out, which takes them in one
// allocation; a fault throws ParseError utf8, or in the value form
// surrogate, with out unchanged.
template <typename A>
constexpr void decode(std::string_view data, std::vector<char32_t, A>& out, Form form = Form::strict) {
    if (const std::optional<ParseCode> fault = check(data, form)) {
        throw ParseError{*fault};
    }
    out.reserve(out.size() + length(data));
    for (std::size_t at = 0; at < data.size();) {
        const Read r = next(data, at, form);
        out.push_back(r.point);
        at += r.size;
    }
}

// A system name's UTF-16 units as UTF-8: a surrogate pair its code point,
// a lone surrogate \u and four lowercase hex digits, appended to out in one
// allocation.
template <typename S>
constexpr void backslashreplace(std::u16string_view name, S& out) {
    constexpr std::string_view hex = "0123456789abcdef";
    const auto pair = [&name](std::size_t i) {
        return i + 1 < name.size() && name[i] >= 0xD800U && name[i] <= 0xDBFFU && name[i + 1] >= 0xDC00U &&
               name[i + 1] <= 0xDFFFU;
    };
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char32_t u = name[i];
        if (pair(i)) {
            bytes += 4;
            ++i;
        } else {
            bytes += u < 0x80U ? 1U : u < 0x800U ? 2U : is_surrogate(u) ? 6U : 3U;
        }
    }
    out.reserve(out.size() + bytes);
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char32_t u = name[i];
        if (pair(i)) {
            append(out, 0x10000U + ((u - 0xD800U) << 10U) + (char32_t{name[i + 1]} - 0xDC00U));
            ++i;
        } else if (is_surrogate(u)) {
            out.push_back('\\');
            out.push_back('u');
            for (unsigned shift = 16; shift != 0;) {
                shift -= 4;
                out.push_back(hex[(u >> shift) & 0xFU]);
            }
        } else {
            append(out, u);
        }
    }
}

}  // namespace utf
}  // namespace qiven3
