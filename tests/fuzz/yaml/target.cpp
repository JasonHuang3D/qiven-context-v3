// fuzz/yaml, the flat YAML reader's fuzz target: the input a file's bytes,
// read by read_flat. A finding is any exception but the reader's own
// refusal and the arena's budget, a crash or a hang; thrown here, a file
// read where the oracle refuses it or refused where the oracle reads it,
// a refusal with another code or line than the oracle's, or fields other
// than the oracle's. The oracle is this file's own, written apart from the
// reader: the file split into lines, each line read in turn and the first
// fault deciding, a key's repeat found as it is read, UTF-8 checked by RFC
// 3629's table, a value's edges and digits read by code point against its
// own lists of Python's whitespace and decimal digits. yaml_input is the
// regression case's entry;
// LLVMFuzzerTestOneInput libFuzzer's, in the fuzz binary alone.

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "import/yaml.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"

namespace {

namespace yaml = qiven3::import::yaml;

void finding(std::string_view what) { throw std::logic_error("fuzz/yaml: " + std::string(what)); }

struct Want {
    std::string key;
    std::string text;
    std::vector<std::string> items;
    bool list = false;
};

struct Expected {
    bool read = true;
    yaml::Code code = yaml::Code::form;
    std::size_t line = 0;
    std::vector<Want> fields;
};

bool well_formed(std::string_view text) {
    std::size_t at = 0;
    while (at < text.size()) {
        const unsigned b = static_cast<unsigned char>(text[at]);
        std::size_t need = 0;
        unsigned lo = 0x80U;
        unsigned hi = 0xBFU;
        if (b <= 0x7FU) {
            need = 0;
        } else if (b >= 0xC2U && b <= 0xDFU) {
            need = 1;
        } else if (b == 0xE0U) {
            need = 2;
            lo = 0xA0U;
        } else if (b == 0xEDU) {
            need = 2;
            hi = 0x9FU;
        } else if (b >= 0xE1U && b <= 0xEFU) {
            need = 2;
        } else if (b == 0xF0U) {
            need = 3;
            lo = 0x90U;
        } else if (b >= 0xF1U && b <= 0xF3U) {
            need = 3;
        } else if (b == 0xF4U) {
            need = 3;
            hi = 0x8FU;
        } else {
            return false;
        }
        if (text.size() - at - 1 < need) {
            return false;
        }
        for (std::size_t k = 1; k <= need; ++k) {
            const unsigned c = static_cast<unsigned char>(text[at + k]);
            if (c < lo || c > hi) {
                return false;
            }
            lo = 0x80U;
            hi = 0xBFU;
        }
        at += need + 1;
    }
    return true;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return out;
}

bool digit(char c) { return c >= '0' && c <= '9'; }

// The lists below are generated from Python 3.15.0's unicodedata, Unicode
// 17.0.0, each code point listed, written apart from the reader's ranges.
// Whitespace as str.isspace reads it.
constexpr std::array<char32_t, 29> spaces = {
    0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x001C, 0x001D, 0x001E, 0x001F, 0x0020, 0x0085, 0x00A0,
    0x1680, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007, 0x2008, 0x2009, 0x200A,
    0x2028, 0x2029, 0x202F, 0x205F, 0x3000,
};
// Each run of ten decimal digits' zero (category Nd).
constexpr std::array<char32_t, 77> zeros = {
    0x00030, 0x00660, 0x006F0, 0x007C0, 0x00966, 0x009E6, 0x00A66, 0x00AE6, 0x00B66, 0x00BE6, 0x00C66, 0x00CE6,
    0x00D66, 0x00DE6, 0x00E50, 0x00ED0, 0x00F20, 0x01040, 0x01090, 0x017E0, 0x01810, 0x01946, 0x019D0, 0x01A80,
    0x01A90, 0x01B50, 0x01BB0, 0x01C40, 0x01C50, 0x0A620, 0x0A8D0, 0x0A900, 0x0A9D0, 0x0A9F0, 0x0AA50, 0x0ABF0,
    0x0FF10, 0x104A0, 0x10D30, 0x10D40, 0x11066, 0x110F0, 0x11136, 0x111D0, 0x112F0, 0x11450, 0x114D0, 0x11650,
    0x116C0, 0x116D0, 0x116DA, 0x11730, 0x118E0, 0x11950, 0x11BF0, 0x11C50, 0x11D50, 0x11DA0, 0x11DE0, 0x11F50,
    0x16130, 0x16A60, 0x16AC0, 0x16B50, 0x16D70, 0x1CCF0, 0x1D7CE, 0x1D7D8, 0x1D7E2, 0x1D7EC, 0x1D7F6, 0x1E140,
    0x1E2F0, 0x1E4F0, 0x1E5F1, 0x1E950, 0x1FBF0,
};

// A well-formed text's code points, by the lead byte's count of ones.
std::vector<char32_t> points(std::string_view text) {
    std::vector<char32_t> out;
    for (std::size_t at = 0; at < text.size();) {
        const unsigned b = static_cast<unsigned char>(text[at]);
        const std::size_t size = b < 0x80U ? 1 : b < 0xE0U ? 2 : b < 0xF0U ? 3 : 4;
        char32_t point = size == 1 ? b : b & (0x7FU >> size);
        for (std::size_t k = 1; k < size; ++k) {
            point = (point << 6U) | (static_cast<unsigned char>(text[at + k]) & 0x3FU);
        }
        out.push_back(point);
        at += size;
    }
    return out;
}

// Whitespace or a control (C0, DEL, C1), the space apart.
bool edge(char32_t c) {
    if (c == 0x20) {
        return false;
    }
    if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) {
        return true;
    }
    for (const char32_t s : spaces) {
        if (c == s) {
            return true;
        }
    }
    return false;
}

// The text as Python's int and float read it: each decimal digit its
// ASCII digit, any other code point past ASCII '?'.
std::string ascii(std::string_view text) {
    std::string out;
    for (const char32_t c : points(text)) {
        char a = c < 0x80 ? static_cast<char>(c) : '?';
        for (const char32_t z : zeros) {
            if (c >= z && c <= z + 9) {
                a = static_cast<char>('0' + (c - z));
            }
        }
        out += a;
    }
    return out;
}

// A float's text: each underscore between two digits dropped, any other
// refusing; then a sign, digits, a point and digits, at least one digit,
// and an exponent with digits.
bool number(std::string_view text) {
    std::string bare;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '_') {
            bare += text[i];
        } else if (i == 0 || i + 1 == text.size() || !digit(text[i - 1]) || !digit(text[i + 1])) {
            return false;
        }
    }
    std::string_view s = bare;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        s.remove_prefix(1);
    }
    const std::string word = lower(s);
    if (word == "inf" || word == "infinity" || word == "nan") {
        return bare.size() == text.size();
    }
    const std::size_t e = s.find_first_of("eE");
    const std::string_view mantissa = s.substr(0, e);
    std::size_t digits = 0;
    std::size_t points = 0;
    for (const char c : mantissa) {
        digits += digit(c) ? 1U : 0U;
        points += c == '.' ? 1U : 0U;
        if (!digit(c) && c != '.') {
            return false;
        }
    }
    if (digits == 0 || points > 1) {
        return false;
    }
    if (e == std::string_view::npos) {
        return true;
    }
    std::string_view exponent = s.substr(e + 1);
    if (!exponent.empty() && (exponent.front() == '+' || exponent.front() == '-')) {
        exponent.remove_prefix(1);
    }
    if (exponent.empty()) {
        return false;
    }
    for (const char c : exponent) {
        if (!digit(c)) {
            return false;
        }
    }
    return true;
}

std::string_view strip(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    return text;
}

// A value's text, or false with its code.
bool scalar(std::string_view v, std::string& out, yaml::Code& code) {
    if (const std::vector<char32_t> each = points(v); edge(each.front()) || edge(each.back())) {
        code = yaml::Code::edge;
        return false;
    }
    if (v.front() == '"' || v.front() == '\'') {
        const std::string_view inner = v.size() >= 2 && v.back() == v.front() ? v.substr(1, v.size() - 2) : v;
        if (inner.size() == v.size() || inner.find(v.front()) != std::string_view::npos ||
            inner.find('\\') != std::string_view::npos) {
            code = yaml::Code::quote;
            return false;
        }
        out = inner;
        return true;
    }
    const std::string word = lower(v);
    if (word == "true" || word == "false" || word == "null" || word == "none" || word == "~" || number(ascii(v))) {
        code = yaml::Code::typed;
        return false;
    }
    out = v;
    return true;
}

Expected oracle(std::string_view file) {
    Expected want;
    const auto refuse = [&](yaml::Code code, std::size_t line) {
        want.read = false;
        want.code = code;
        want.line = line;
        want.fields.clear();
    };
    if (file.size() > yaml::max_bytes) {
        refuse(yaml::Code::too_large, 0);
        return want;
    }
    std::vector<std::string_view> lines;
    std::size_t from = 0;
    for (std::size_t i = 0; i <= file.size(); ++i) {
        if (i == file.size() || file[i] == '\n') {
            lines.push_back(file.substr(from, i - from));
            from = i + 1;
        }
    }
    std::set<std::string> seen;
    bool open = false;
    for (std::size_t n = 1; n <= lines.size(); ++n) {
        const std::string_view line = lines[n - 1];
        yaml::Code code = yaml::Code::form;
        std::string out;
        if (!well_formed(line)) {
            refuse(yaml::Code::utf8, n);
        } else if (line.find('\t') != std::string_view::npos || line.find('\r') != std::string_view::npos) {
            refuse(yaml::Code::control, n);
        } else if (line.empty() || line.front() == '#') {
            continue;
        } else if (line.front() == ' ') {
            refuse(yaml::Code::indented, n);
        } else if (const std::string_view s = strip(line); s.front() == '-') {
            if (s.substr(0, 2) != "- ") {
                refuse(yaml::Code::form, n);
            } else if (!open) {
                refuse(yaml::Code::outside_list, n);
            } else if (!scalar(strip(s.substr(2)), out, code)) {
                refuse(code, n);
            } else {
                want.fields.back().items.push_back(out);
                continue;
            }
        } else {
            const std::size_t colon = s.find(':');
            const std::string key(s.substr(0, colon == std::string_view::npos ? 0 : colon));
            const std::string_view rest = colon == std::string_view::npos ? "" : s.substr(colon + 1);
            bool keyed = !key.empty();
            for (const char c : key) {
                keyed = keyed && ((c >= 'a' && c <= 'z') || digit(c) || c == '_');
            }
            if (!keyed || (!rest.empty() && rest.front() != ' ')) {
                refuse(yaml::Code::form, n);
            } else if (!seen.insert(key).second) {
                refuse(yaml::Code::duplicate_key, n);
            } else if (rest.empty()) {
                want.fields.push_back(Want{key, "", {}, true});
                open = true;
                continue;
            } else if (!scalar(strip(rest), out, code)) {
                refuse(code, n);
            } else {
                want.fields.push_back(Want{key, out, {}, false});
                open = false;
                continue;
            }
        }
        break;
    }
    return want;
}

}  // namespace

namespace qiven3::fuzz {

int yaml_input(const std::uint8_t* data, std::size_t size) {
    const std::string file(data, data + size);
    const Expected want = oracle(file);
    // Room for every line a field and its order, 32 and 4 bytes, from 2
    // bytes of input at least, an item's 8 from 3, and the text's copy.
    std::vector<std::byte> pages((24 * size) + 4096);
    memory::Arena arena(pages);
    try {
        const yaml::Fields fields = yaml::read_flat(file, arena);
        if (!want.read) {
            finding("a file read that the oracle refuses");
        }
        if (fields.size() != want.fields.size()) {
            finding("a count of fields not the oracle's");
        }
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Want& w = want.fields[i];
            if (fields.key(i) != w.key || fields.list(i) != w.list || fields.text(i) != w.text ||
                fields.count(i) != w.items.size()) {
                finding("a field not the oracle's");
            }
            for (std::size_t k = 0; k < fields.count(i); ++k) {
                if (fields.item(i, k) != w.items[k]) {
                    finding("an item not the oracle's");
                }
            }
        }
    } catch (const yaml::YamlError& e) {
        if (want.read) {
            finding("a file refused that the oracle reads");
        }
        if (e.code != want.code || e.line != want.line) {
            finding("a refusal's code or line not the oracle's");
        }
    }
    return 0;
}

}  // namespace qiven3::fuzz

#if defined(QIVEN3_FUZZ_BINARY)
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return qiven3::fuzz::yaml_input(data, size);
}
#endif
