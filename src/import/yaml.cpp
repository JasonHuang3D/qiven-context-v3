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

// Past the digits from at, single underscores between them, or npos when
// no digit is there.
std::size_t past_digits(std::string_view text, std::size_t at) noexcept {
    if (at >= text.size() || !is_digit(text[at])) {
        return npos;
    }
    std::size_t i = at + 1;
    while (i < text.size()) {
        if (is_digit(text[i])) {
            ++i;
        } else if (text[i] == '_' && i + 1 < text.size() && is_digit(text[i + 1])) {
            i += 2;
        } else {
            break;
        }
    }
    return i;
}

// Whether text reads as a float's text: a sign, then inf, infinity or nan
// in any case, or digits, a point and digits, either side's digits
// optional but not both, then an exponent.
bool is_number(std::string_view text) noexcept {
    std::size_t i = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
        ++i;
    }
    const std::string_view word = text.substr(i);
    if (is_word(word, "inf") || is_word(word, "infinity") || is_word(word, "nan")) {
        return true;
    }
    bool digits = false;
    if (const std::size_t past = past_digits(text, i); past != npos) {
        digits = true;
        i = past;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        if (const std::size_t past = past_digits(text, i); past != npos) {
            digits = true;
            i = past;
        }
    }
    if (!digits) {
        return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        i = past_digits(text, i);
        if (i == npos) {
            return false;
        }
    }
    return i == text.size();
}

// Whether the source system reads a plain scalar as other than text.
bool is_typed(std::string_view text) noexcept {
    return is_word(text, "true") || is_word(text, "false") || is_word(text, "null") || is_word(text, "none") ||
           text == "~" || is_number(text);
}

// A value's text, trimmed and not empty, or its fault's code.
std::optional<Code> value(std::string_view text, std::string_view& out) noexcept {
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
    static constexpr std::array<std::string_view, 9> names = {
        "utf8", "control", "indented", "form", "duplicate_key", "outside_list", "quote", "typed", "too_large",
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
