#include "import/yaml.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "memory/memory.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::import::yaml {

namespace {

constexpr std::size_t npos = std::string_view::npos;

struct Fault {
    Code code;
    std::size_t line;
};

constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_key_byte(char c) noexcept { return (c >= 'a' && c <= 'z') || is_digit(c) || c == '_'; }

// The two tables below are generated from Python 3.15.0's unicodedata,
// Unicode 17.0.0, the readings of the source system's strip, int and float.
struct Range {
    char32_t first;
    char32_t last;
};

// The code points no value or item begins or ends with, as closed ranges:
// whitespace as Python's str.isspace reads it, which the source system's
// strip trims, and the controls (category Cc), the space apart.
constexpr std::array<Range, 8> edge_points = {{
    {0x0000, 0x001F}, {0x007F, 0x00A0}, {0x1680, 0x1680}, {0x2000, 0x200A},
    {0x2028, 0x2029}, {0x202F, 0x202F}, {0x205F, 0x205F}, {0x3000, 0x3000},
}};

// The decimal digits (category Nd), which Python's int and float read in
// any script: each run of ten's zero, a digit's value its distance from it.
constexpr std::array<char32_t, 77> digit_zeros = {
    0x00030, 0x00660, 0x006F0, 0x007C0, 0x00966, 0x009E6, 0x00A66, 0x00AE6,
    0x00B66, 0x00BE6, 0x00C66, 0x00CE6, 0x00D66, 0x00DE6, 0x00E50, 0x00ED0,
    0x00F20, 0x01040, 0x01090, 0x017E0, 0x01810, 0x01946, 0x019D0, 0x01A80,
    0x01A90, 0x01B50, 0x01BB0, 0x01C40, 0x01C50, 0x0A620, 0x0A8D0, 0x0A900,
    0x0A9D0, 0x0A9F0, 0x0AA50, 0x0ABF0, 0x0FF10, 0x104A0, 0x10D30, 0x10D40,
    0x11066, 0x110F0, 0x11136, 0x111D0, 0x112F0, 0x11450, 0x114D0, 0x11650,
    0x116C0, 0x116D0, 0x116DA, 0x11730, 0x118E0, 0x11950, 0x11BF0, 0x11C50,
    0x11D50, 0x11DA0, 0x11DE0, 0x11F50, 0x16130, 0x16A60, 0x16AC0, 0x16B50,
    0x16D70, 0x1CCF0, 0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC, 0x1D7F6, 0x1E140,
    0x1E2F0, 0x1E4F0, 0x1E5F1, 0x1E950, 0x1FBF0,
};

bool is_edge(char32_t point) noexcept {
    return std::any_of(edge_points.begin(), edge_points.end(),
                       [point](Range range) { return point >= range.first && point <= range.last; });
}

// Whether a valid, non-empty text begins or ends with an edge code point.
bool has_edge(std::string_view text) noexcept {
    std::size_t last = text.size() - 1;
    while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xC0U) == 0x80U) {
        --last;
    }
    return is_edge(utf::next(text, 0).point) || is_edge(utf::next(text, last).point);
}

// A code point as Python's int and float read it: ASCII as itself, a
// decimal digit as its ASCII digit, any other as '?', which no number holds.
char ascii_of(char32_t point) noexcept {
    if (point < 0x80) {
        return static_cast<char>(point);
    }
    for (const char32_t zero : digit_zeros) {
        if (point >= zero && point - zero < 10) {
            return static_cast<char>('0' + (point - zero));
        }
    }
    return '?';
}

// A valid text read a code point at a time: the one at text[at] as
// ascii_of reads it, or '\0' past the text, which no rule below takes.
char ascii_at(std::string_view text, std::size_t at) noexcept {
    return at < text.size() ? ascii_of(utf::next(text, at).point) : '\0';
}

// The place past the code point at text[at], at < text.size().
std::size_t past(std::string_view text, std::size_t at) noexcept {
    return at + std::max<std::size_t>(utf::next(text, at).size, 1);
}

// Whether text from at is word, a lower-case ASCII word, in any case.
bool rest_is(std::string_view text, std::size_t at, std::string_view word) noexcept {
    for (const char w : word) {
        const char c = ascii_at(text, at);
        if ((c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c) != w) {
            return false;
        }
        at = past(text, at);
    }
    return at == text.size();
}

// Text without its spaces at either end.
std::string_view trim(std::string_view text) noexcept {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// Whether text is word, a lower-case ASCII word, in any case.
bool is_word(std::string_view text, std::string_view word) noexcept {
    return text.size() == word.size() && std::equal(text.begin(), text.end(), word.begin(), [](char a, char b) {
               return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
           });
}

// Moves at past the digits there, single underscores between them; false,
// at not moved, when no digit is there.
bool past_digits(std::string_view text, std::size_t& at) noexcept {
    if (!is_digit(ascii_at(text, at))) {
        return false;
    }
    at = past(text, at);
    for (;;) {
        const char c = ascii_at(text, at);
        if (is_digit(c)) {
            at = past(text, at);
            continue;
        }
        if (c != '_' || !is_digit(ascii_at(text, past(text, at)))) {
            return true;
        }
        at = past(text, at);
    }
}

// Moves at past a sign there.
void past_sign(std::string_view text, std::size_t& at) noexcept {
    if (const char c = ascii_at(text, at); c == '+' || c == '-') {
        at = past(text, at);
    }
}

// Whether text reads as Python's float reads it, each code point as
// ascii_of reads it (and so as its int reads it too): a sign, then inf,
// infinity or nan in any case, or digits, a point and digits, either
// side's digits optional but not both, then an exponent.
bool is_number(std::string_view text) noexcept {
    std::size_t at = 0;
    past_sign(text, at);
    if (rest_is(text, at, "inf") || rest_is(text, at, "infinity") || rest_is(text, at, "nan")) {
        return true;
    }
    bool digits = past_digits(text, at);
    if (ascii_at(text, at) == '.') {
        at = past(text, at);
        digits = past_digits(text, at) || digits;
    }
    if (!digits) {
        return false;
    }
    if (const char e = ascii_at(text, at); e == 'e' || e == 'E') {
        at = past(text, at);
        past_sign(text, at);
        if (!past_digits(text, at)) {
            return false;
        }
    }
    return at == text.size();
}

// Whether the source system reads a plain scalar as other than text. The
// words are compared as ASCII: no character past ASCII lowers, as Python's
// lower does, into one of them (the Kelvin sign alone lowers to ASCII, k).
bool is_typed(std::string_view text) noexcept {
    return is_word(text, "true") || is_word(text, "false") || is_word(text, "null") || is_word(text, "none") ||
           text == "~" || is_number(text);
}

// A value's text, trimmed and not empty, or its fault's code.
std::optional<Code> value(std::string_view text, std::string_view& out) noexcept {
    if (has_edge(text)) {
        return Code::edge;
    }
    const char quote = text.front();
    if (quote == '"' || quote == '\'') {
        if (text.size() < 2 || text.back() != quote) {
            return Code::quote;
        }
        out = text.substr(1, text.size() - 2);
        if (out.find(quote) != npos || out.find('\\') != npos) {
            return Code::quote;
        }
        return std::nullopt;
    }
    if (is_typed(text)) {
        return Code::typed;
    }
    out = text;
    return std::nullopt;
}

// The first line holding a key a second time, or none: the keys sorted
// with their lines, each key's second line the candidate.
std::optional<std::size_t> duplicate(const Fields& fields, memory::Arena& arena) {
    if (fields.size() < 2) {
        return std::nullopt;
    }
    memory::Vector<std::uint32_t> order{memory::Allocator<std::uint32_t>(arena)};
    order.reserve(fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        order.push_back(static_cast<std::uint32_t>(i));
    }
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        return fields.key(a) != fields.key(b) ? fields.key(a) < fields.key(b) : a < b;
    });
    std::optional<std::size_t> first;
    for (std::size_t k = 1; k < order.size(); ++k) {
        if (fields.key(order[k]) == fields.key(order[k - 1]) && (!first || fields.line(order[k]) < *first)) {
            first = fields.line(order[k]);
        }
    }
    return first;
}

}  // namespace

std::string_view name(Code code) noexcept {
    static constexpr std::array<std::string_view, 10> names = {
        "utf8", "control", "indented", "form", "duplicate_key", "outside_list", "edge", "quote", "typed", "too_large",
    };
    return names.at(static_cast<std::size_t>(code));
}

std::optional<std::size_t> Fields::find(std::string_view key) const noexcept {
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        if (view(fields_[i].key) == key) {
            return i;
        }
    }
    return std::nullopt;
}

Fields read_flat(std::string_view bytes, memory::Arena& arena) {
    if (bytes.size() > max_bytes) {
        throw YamlError{Code::too_large, 0};
    }
    // The room: a field for each line opening with a key's byte, an item
    // for each opening with a dash; exact for a file read whole.
    std::size_t keys = 0;
    std::size_t dashes = 0;
    for (std::size_t at = 0; at < bytes.size();) {
        keys += is_key_byte(bytes[at]) ? 1U : 0U;
        dashes += bytes[at] == '-' ? 1U : 0U;
        const std::size_t lf = bytes.find('\n', at);
        at = lf == npos ? bytes.size() : lf + 1;
    }
    Fields fields(arena);
    fields.fields_.reserve(keys);
    fields.items_.reserve(dashes);
    fields.text_.reserve(bytes.size());

    std::optional<Fault> fault;
    bool open = false;  // a list open: the last field's
    std::uint32_t line = 0;
    for (std::size_t at = 0; at < bytes.size() && !fault;) {
        const std::size_t end = std::min(bytes.find('\n', at), bytes.size());
        const std::string_view raw = bytes.substr(at, end - at);
        at = end + 1;
        ++line;
        if (utf::check(raw).has_value()) {
            fault = Fault{Code::utf8, line};
        } else if (raw.find_first_of("\t\r") != npos) {
            fault = Fault{Code::control, line};
        } else if (raw.empty() || raw.front() == '#') {
            continue;  // the empty line, a comment
        } else if (raw.front() == ' ') {
            fault = Fault{Code::indented, line};
        } else {
            const std::string_view text = raw.substr(0, raw.find_last_not_of(' ') + 1);
            std::string_view out;
            if (text.front() == '-') {
                if (text.size() < 2 || text[1] != ' ') {
                    fault = Fault{Code::form, line};
                } else if (!open) {
                    fault = Fault{Code::outside_list, line};
                } else if (const std::optional<Code> code = value(trim(text.substr(2)), out)) {
                    fault = Fault{*code, line};
                } else {
                    fields.items_.push_back(fields.keep(out));
                    ++fields.fields_.back().count;
                }
                continue;
            }
            std::size_t colon = 0;
            while (colon < text.size() && is_key_byte(text[colon])) {
                ++colon;
            }
            if (colon == 0 || colon == text.size() || text[colon] != ':' ||
                (colon + 1 < text.size() && text[colon + 1] != ' ')) {
                fault = Fault{Code::form, line};
                continue;
            }
            Field& field = fields.fields_.emplace_back();
            field.key = fields.keep(text.substr(0, colon));
            field.line = line;
            open = colon + 1 == text.size();
            if (open) {
                field.list = true;
                field.first = static_cast<std::uint32_t>(fields.items_.size());
            } else if (const std::optional<Code> code = value(trim(text.substr(colon + 1)), out)) {
                fault = Fault{*code, line};
            } else {
                field.text = fields.keep(out);
            }
        }
    }
    // A key a second time: each key read lies on a line no later than the
    // first other fault, and on that line the key is read first.
    if (const std::optional<std::size_t> again = duplicate(fields, arena)) {
        throw YamlError{Code::duplicate_key, *again};
    }
    if (fault) {
        throw YamlError{fault->code, fault->line};
    }
    return fields;
}

}  // namespace qiven3::import::yaml
