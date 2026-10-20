#include "driver/wire.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>

namespace qiven3::drive {
namespace {

constexpr int max_depth = 64;

[[noreturn]] void wrong(std::string_view what, const char* kind) {
    throw WireError(std::string(what) + ": not " + kind);
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Value whole() {
        Value value = next(1);
        space();
        if (at_ < text_.size()) {
            fail("text after the value");
        }
        return value;
    }

private:
    [[noreturn]] void fail(const std::string& why) const {
        throw WireError("the request at byte " + std::to_string(at_) + ": " + why);
    }

    void space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }

    bool take(char c) {
        space();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!take(c)) {
            fail(std::string("expected '") + c + "'");
        }
    }

    bool word(std::string_view w) {
        if (text_.substr(at_, w.size()) == w) {
            at_ += w.size();
            return true;
        }
        return false;
    }

    Value next(int depth) {
        if (depth > max_depth) {
            fail("nested too deep");
        }
        space();
        if (at_ >= text_.size()) {
            fail("no value");
        }
        const char c = text_[at_];
        if (c == '{') {
            ++at_;
            return object(depth);
        }
        if (c == '[') {
            ++at_;
            Value items = Value::array();
            if (take(']')) {
                return items;
            }
            do {
                items.push(next(depth + 1));
            } while (take(','));
            expect(']');
            return items;
        }
        if (c == '"') {
            return Value(string());
        }
        if (word("true")) {
            return Value(true);
        }
        if (word("false")) {
            return Value(false);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return number();
        }
        fail("no value");
    }

    Value object(int depth) {
        Value members;
        if (take('}')) {
            return members;
        }
        do {
            space();
            if (at_ >= text_.size() || text_[at_] != '"') {
                fail("expected a key");
            }
            std::string key = string();
            if (members.find(key) != nullptr) {
                fail("the key '" + key + "' twice");
            }
            expect(':');
            members.set(std::move(key), next(depth + 1));
        } while (take(','));
        expect('}');
        return members;
    }

    std::uint32_t hex4() {
        if (at_ + 4 > text_.size()) {
            fail("a short \\u escape");
        }
        std::uint32_t code = 0;
        const auto [end, error] = std::from_chars(text_.data() + at_, text_.data() + at_ + 4, code, 16);
        if (error != std::errc() || end != text_.data() + at_ + 4) {
            fail("a malformed \\u escape");
        }
        at_ += 4;
        return code;
    }

    std::string string() {
        ++at_;  // the opening quote
        std::string out;
        while (true) {
            if (at_ >= text_.size()) {
                fail("an unterminated string");
            }
            const char c = text_[at_++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("a raw control character in a string");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) {
                fail("an unterminated escape");
            }
            const char e = text_[at_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t code = hex4();
                if (code >= 0xD800 && code <= 0xDBFF && word("\\u")) {
                    const std::uint32_t low = hex4();
                    if (low < 0xDC00 || low > 0xDFFF) {
                        fail("a high surrogate without its low one");
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                } else if (code >= 0xD800 && code <= 0xDFFF) {
                    fail("a lone surrogate");
                }
                append_utf8(out, code);
                break;
            }
            default: fail("an unknown escape");
            }
        }
    }

    Value number() {
        const std::size_t start = at_;
        if (text_[at_] == '-') {
            ++at_;
        }
        const std::size_t digits = at_;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            ++at_;
        }
        if (at_ == digits || (text_[digits] == '0' && at_ - digits > 1)) {
            fail("a malformed integer");
        }
        if (at_ < text_.size() && (text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E')) {
            fail("a float: the wire has integers alone");
        }
        std::int64_t value = 0;
        const auto [end, error] = std::from_chars(text_.data() + start, text_.data() + at_, value);
        if (error != std::errc() || end != text_.data() + at_) {
            fail("an integer out of range");
        }
        return Value(value);
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

void encode_string(std::string& out, const std::string& text) {
    static constexpr char digits[] = "0123456789abcdef";
    out += '"';
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\f': out += "\\f"; break;
        case '\r': out += "\\r"; break;
        default:
            if (u < 0x20) {
                out += "\\u00";
                out += digits[u >> 4];
                out += digits[u & 0xF];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void encode_into(std::string& out, const Value& value) {
    switch (value.kind()) {
    case Value::Kind::boolean: out += value.as_boolean("") ? "true" : "false"; return;
    case Value::Kind::integer: out += std::to_string(value.as_integer("")); return;
    case Value::Kind::string: encode_string(out, value.as_string("")); return;
    case Value::Kind::array: {
        out += '[';
        bool first = true;
        for (const Value& item : value.as_array("")) {
            if (!first) {
                out += ',';
            }
            first = false;
            encode_into(out, item);
        }
        out += ']';
        return;
    }
    case Value::Kind::object: {
        std::vector<std::string> keys = value.keys();
        std::sort(keys.begin(), keys.end());  // by bytes, as unsigned char
        out += '{';
        bool first = true;
        for (const std::string& key : keys) {
            if (!first) {
                out += ',';
            }
            first = false;
            encode_string(out, key);
            out += ':';
            encode_into(out, *value.find(key));
        }
        out += '}';
        return;
    }
    }
}

}  // namespace

Value Value::array(std::vector<Value> items) {
    Value value;
    value.kind_ = Kind::array;
    value.items_ = std::move(items);
    return value;
}

bool Value::as_boolean(std::string_view what) const {
    if (kind_ != Kind::boolean) {
        wrong(what, "true or false");
    }
    return boolean_;
}

std::int64_t Value::as_integer(std::string_view what) const {
    if (kind_ != Kind::integer) {
        wrong(what, "an integer");
    }
    return integer_;
}

const std::string& Value::as_string(std::string_view what) const {
    if (kind_ != Kind::string) {
        wrong(what, "a string");
    }
    return string_;
}

const std::vector<Value>& Value::as_array(std::string_view what) const {
    if (kind_ != Kind::array) {
        wrong(what, "an array");
    }
    return items_;
}

Value& Value::push(Value item) {
    if (kind_ != Kind::array) {
        wrong("push", "an array");
    }
    items_.push_back(std::move(item));
    return *this;
}

const Value* Value::find(std::string_view key) const {
    if (kind_ != Kind::object) {
        return nullptr;
    }
    const auto found = std::find(keys_.begin(), keys_.end(), key);
    return found == keys_.end() ? nullptr : &items_[static_cast<std::size_t>(found - keys_.begin())];
}

Value& Value::set(std::string key, Value value) {
    if (kind_ != Kind::object) {
        wrong("set", "an object");
    }
    const auto found = std::find(keys_.begin(), keys_.end(), key);
    if (found != keys_.end()) {
        items_[static_cast<std::size_t>(found - keys_.begin())] = std::move(value);
    } else {
        keys_.push_back(std::move(key));
        items_.push_back(std::move(value));
    }
    return *this;
}

Value parse(std::string_view text) { return Parser(text).whole(); }

std::string encode(const Value& value) {
    std::string out;
    encode_into(out, value);
    return out;
}

std::string to_hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto u = static_cast<unsigned char>(c);
        out += digits[u >> 4];
        out += digits[u & 0xF];
    }
    return out;
}

std::string from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw WireError("hexadecimal of an odd length");
    }
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
        if (error != std::errc() || end != hex.data() + i + 2) {
            throw WireError("a character that is no hexadecimal digit");
        }
        out += static_cast<char>(value);
    }
    return out;
}

}  // namespace qiven3::drive
