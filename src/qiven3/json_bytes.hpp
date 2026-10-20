#pragma once

// json's bytes, behind json.hpp, which includes it: encode, the canonical
// bytes of a value, and scan, the parser's grammar over text whose BOM and
// UTF-8 its caller has checked (steps 1 and 2), as the depth scan (step 3)
// and the pass (step 4) of the strict parser.
//
// thread: none

#include "qiven3/json.hpp"

namespace qiven3::json {
namespace detail {

constexpr bool space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
constexpr bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

constexpr std::size_t width(std::int64_t v) noexcept {
    std::size_t n = v < 0 ? 2 : 1;
    for (std::int64_t rest = v / 10; rest != 0; rest /= 10) {
        ++n;
    }
    return n;
}

// The bytes of a string's canonical form, its quotes counted.
constexpr std::size_t quoted(std::string_view s) noexcept {
    std::size_t n = 2;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        n += c == '"' || c == '\\' || c == '\b' || c == '\t' || c == '\n' || c == '\f' || c == '\r' ? 2U
             : u < 0x20U                                                                         ? 6U
                                                                                                 : 1U;
    }
    return n;
}

template <typename S>
constexpr void put_integer(S& out, std::int64_t v) {
    std::array<char, 20> digits{};
    std::size_t n = 0;
    std::uint64_t rest = v < 0 ? 0U - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
    do {
        digits.at(n++) = static_cast<char>('0' + static_cast<int>(rest % 10U));
        rest /= 10U;
    } while (rest != 0);
    if (v < 0) {
        out.push_back('-');
    }
    while (n > 0) {
        out.push_back(digits.at(--n));
    }
}

// FORMAT's escapes: \" \\ \b \t \n \f \r, \u00XX in lowercase for the other
// controls below U+0020, and every other byte raw.
template <typename S>
constexpr void put_string(S& out, std::string_view s) {
    constexpr std::string_view hex = "0123456789abcdef";
    out.push_back('"');
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        const char short_form = c == '"'    ? '"'
                                : c == '\\' ? '\\'
                                : c == '\b' ? 'b'
                                : c == '\t' ? 't'
                                : c == '\n' ? 'n'
                                : c == '\f' ? 'f'
                                : c == '\r' ? 'r'
                                            : '\0';
        if (short_form != '\0') {
            out.push_back('\\');
            out.push_back(short_form);
        } else if (u < 0x20U) {
            out.append("\\u00");
            out.push_back(hex[u >> 4U]);
            out.push_back(hex[u & 0xFU]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

}  // namespace detail

// The value's canonical bytes appended to out, a string type, in one
// allocation: keys in code point order, no space, FORMAT's escapes,
// integers in decimal. Its one other allocation, from out's allocator,
// holds an object's sorted members while it is written.
template <typename A, typename S>
constexpr void encode(const Basic<A>& value, S& out) {
    if (value.empty()) {
        memory::fail_fast("json::encode: an empty value");
    }
    std::size_t bytes = 0;
    std::size_t members = 0;
    for (Index k = 0; k < value.size(); ++k) {
        switch (value.kind(k)) {
        case Kind::null: bytes += 4; break;
        case Kind::boolean: bytes += value.boolean(k) ? 4 : 5; break;
        case Kind::integer: bytes += detail::width(value.integer(k)); break;
        case Kind::string: bytes += detail::quoted(value.text(k)); break;
        case Kind::array: bytes += value.count(k) == 0 ? 2 : value.count(k) + 1; break;
        case Kind::object:
            bytes += value.count(k) == 0 ? 2 : 2 * value.count(k) + 1;
            members += value.count(k);
            break;
        }
    }
    using Order = std::vector<Index, typename std::allocator_traits<typename S::allocator_type>::template rebind_alloc<Index>>;
    Order order(out.get_allocator());
    order.reserve(members);
    out.reserve(out.size() + bytes);

    struct Frame {
        Index node = 0;
        Index cursor = 0;  // an array's next item; an object's next member in order
        std::size_t base = 0;
    };
    std::array<Frame, max_depth> frames{};
    std::size_t open = 0;
    const auto emit = [&](Index i) {
        switch (value.kind(i)) {
        case Kind::null: out.append("null"); break;
        case Kind::boolean: out.append(value.boolean(i) ? "true" : "false"); break;
        case Kind::integer: detail::put_integer(out, value.integer(i)); break;
        case Kind::string: detail::put_string(out, value.text(i)); break;
        case Kind::array:
            out.push_back('[');
            frames.at(open++) = Frame{i, i + 1, 0};
            break;
        case Kind::object: {
            out.push_back('{');
            const std::size_t base = order.size();
            for (Index k = i + 1; k < value.next(i); k = value.next(k + 1)) {
                order.push_back(k);
            }
            std::sort(order.begin() + static_cast<std::ptrdiff_t>(base), order.end(), [&value](Index a, Index b) {
                return utf::compare(value.text(a), value.text(b)) < 0;
            });
            frames.at(open++) = Frame{i, 0, base};
            break;
        }
        }
    };
    emit(0);
    while (open > 0) {
        Frame& f = frames.at(open - 1);
        if (value.kind(f.node) == Kind::array) {
            if (f.cursor == value.next(f.node)) {
                out.push_back(']');
                --open;
                continue;
            }
            if (f.cursor != f.node + 1) {
                out.push_back(',');
            }
            const Index item = f.cursor;
            f.cursor = value.next(item);
            emit(item);
        } else {
            if (f.cursor == value.count(f.node)) {
                out.push_back('}');
                order.resize(f.base);
                --open;
                continue;
            }
            if (f.cursor != 0) {
                out.push_back(',');
            }
            const Index key = order.at(f.base + f.cursor);
            ++f.cursor;
            detail::put_string(out, value.text(key));
            out.push_back(':');
            emit(key + 1);
        }
    }
}

namespace detail {

// What a value takes: its nodes, its text's bytes, and the most members of
// one object that closed.
struct Tally {
    std::size_t nodes = 0;
    std::size_t text = 0;
    std::size_t members = 0;
};

// Step 3, as written: outside a string each [ or { opens a level and each
// ] or } closes one; a string runs from " to the next ", a backslash in it
// skipping the next character.
constexpr bool too_deep(std::string_view s, std::size_t limit) noexcept {
    std::size_t level = 0;
    std::size_t closed = 0;  // closes below level 0, which the pass refuses
    bool in = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (in) {
            i += c == '\\' ? 1U : 0U;
            in = c != '"';
        } else if (c == '"') {
            in = true;
        } else if (c == '[' || c == '{') {
            if (closed > 0) {
                --closed;
            } else if (++level > limit) {
                return true;
            }
        } else if (c == ']' || c == '}') {
            if (level > 0) {
                --level;
            } else {
                ++closed;
            }
        }
    }
    return false;
}

constexpr std::optional<char32_t> hex4(std::string_view s, std::size_t at) noexcept {
    if (at > s.size() || s.size() - at < 4) {
        return std::nullopt;
    }
    char32_t v = 0;
    for (std::size_t i = at; i < at + 4; ++i) {
        const char c = s[i];
        const unsigned d = c >= '0' && c <= '9'   ? static_cast<unsigned>(c - '0')
                           : c >= 'a' && c <= 'f' ? static_cast<unsigned>(c - 'a' + 10)
                           : c >= 'A' && c <= 'F' ? static_cast<unsigned>(c - 'A' + 10)
                                                  : 16U;
        if (d == 16U) {
            return std::nullopt;
        }
        v = (v << 4U) | d;
    }
    return v;
}

constexpr std::size_t utf8_size(char32_t p) noexcept { return p < 0x80U ? 1 : p < 0x800U ? 2 : p < 0x10000U ? 3 : 4; }

}  // namespace detail

namespace detail {

// Step 4: one pass over the text in document order, as json.loads with the
// four hooks reads it, with no recursion: an explicit stack of the levels
// open. Without a writer it only tallies; with one it builds, and checks
// each object's keys when it closes.
template <typename A>
class PassView {
public:
    using Keys = std::vector<Index, typename std::allocator_traits<A>::template rebind_alloc<Index>>;

    constexpr PassView(std::string_view s, std::size_t limit, Writer<A>* writer, const Basic<A>* value, Keys* keys)
        : s_(s), limit_(limit), w_(writer), value_(value), keys_(keys) {}

    constexpr std::optional<ParseCode> run() {
        enum class Want : std::uint8_t { value, key, after };
        Want want = Want::value;
        skip();
        while (true) {
            if (want == Want::key) {
                if (at_ >= s_.size() || s_[at_] != '"') {
                    return ParseCode::syntax;
                }
                ++members_.at(open_ - 1);
                if (const auto fault = string()) {
                    return fault;
                }
                skip();
                if (at_ >= s_.size() || s_[at_] != ':') {
                    return ParseCode::syntax;
                }
                ++at_;
                skip();
                want = Want::value;
                continue;
            }
            if (want == Want::value) {
                if (at_ < s_.size() && (s_[at_] == '[' || s_[at_] == '{')) {
                    const bool object = s_[at_] == '{';
                    if (open_ == limit_) {
                        return ParseCode::depth;
                    }
                    open(object ? Kind::object : Kind::array);
                    ++at_;
                    skip();
                    if (at_ < s_.size() && s_[at_] == (object ? '}' : ']')) {
                        ++at_;
                        if (const auto fault = close()) {
                            return fault;
                        }
                        want = Want::after;
                    } else {
                        want = object ? Want::key : Want::value;
                    }
                    continue;
                }
                if (const auto fault = scalar()) {
                    return fault;
                }
            }
            // After a value.
            skip();
            if (open_ == 0) {
                return at_ == s_.size() ? std::nullopt : std::optional<ParseCode>(ParseCode::syntax);
            }
            const bool object = kinds_.at(open_ - 1) == Kind::object;
            if (at_ < s_.size() && s_[at_] == ',') {
                ++at_;
                skip();
                want = object ? Want::key : Want::value;
            } else if (at_ < s_.size() && s_[at_] == (object ? '}' : ']')) {
                ++at_;
                if (const auto fault = close()) {
                    return fault;
                }
                want = Want::after;
            } else {
                return ParseCode::syntax;
            }
        }
    }

    [[nodiscard]] constexpr const Tally& tally() const noexcept { return tally_; }

private:
    constexpr void skip() noexcept {
        while (at_ < s_.size() && space(s_[at_])) {
            ++at_;
        }
    }
    [[nodiscard]] constexpr bool ahead(std::string_view word) const noexcept {
        return s_.substr(at_, word.size()) == word;
    }
    constexpr void node() {
        if (w_ == nullptr) {
            ++tally_.nodes;
        }
    }
    constexpr void open(Kind kind) {
        kinds_.at(open_) = kind;
        members_.at(open_) = 0;
        starts_.at(open_) = static_cast<Index>(w_ == nullptr ? tally_.nodes : value_->size());
        ++open_;
        node();
        if (w_ != nullptr) {
            w_->open(kind);
        }
    }
    // A repeated key is duplicate_key when its object closes.
    constexpr std::optional<ParseCode> close() {
        --open_;
        if (kinds_.at(open_) == Kind::object) {
            tally_.members = std::max(tally_.members, members_.at(open_));
        }
        if (w_ == nullptr) {
            return std::nullopt;
        }
        w_->close();
        if (kinds_.at(open_) != Kind::object || members_.at(open_) < 2) {
            return std::nullopt;
        }
        const Index start = starts_.at(open_);
        keys_->clear();
        for (Index k = start + 1; k < value_->next(start); k = value_->next(k + 1)) {
            keys_->push_back(k);
        }
        const auto by_text = [this](Index a, Index b) { return value_->text(a) < value_->text(b); };
        std::sort(keys_->begin(), keys_->end(), by_text);
        for (std::size_t i = 1; i < keys_->size(); ++i) {
            if (value_->text(keys_->at(i - 1)) == value_->text(keys_->at(i))) {
                return ParseCode::duplicate_key;
            }
        }
        return std::nullopt;
    }
    constexpr void put(char c) {
        if (w_ == nullptr) {
            ++tally_.text;
        } else {
            w_->byte(c);
        }
    }
    constexpr void put_point(char32_t p) {
        if (w_ == nullptr) {
            tally_.text += utf8_size(p);
        } else {
            w_->point(p);
        }
    }
    constexpr std::optional<ParseCode> scalar() {
        if (at_ >= s_.size()) {
            return ParseCode::syntax;
        }
        const char c = s_[at_];
        if (c == '"') {
            return string();
        }
        if (c == '-' || digit(c)) {
            return ahead("-Infinity") ? std::optional<ParseCode>(ParseCode::constant) : number();
        }
        if (ahead("NaN") || ahead("Infinity")) {
            return ParseCode::constant;
        }
        for (const std::string_view word : {std::string_view("null"), std::string_view("true"), std::string_view("false")}) {
            if (ahead(word)) {
                at_ += word.size();
                node();
                if (w_ != nullptr && word == "null") {
                    w_->null();
                } else if (w_ != nullptr) {
                    w_->boolean(word == "true");
                }
                return std::nullopt;
            }
        }
        return ParseCode::syntax;
    }
    // -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)? as json.loads matches
    // it; a fraction or an exponent is float, more than 16 digits or a
    // value past 2^53 - 1 int_range, each where it is read.
    constexpr std::optional<ParseCode> number() {
        const std::size_t n = s_.size();
        const bool negative = s_[at_] == '-';
        const std::size_t first = at_ + (negative ? 1U : 0U);
        at_ = first;
        if (at_ < n && s_[at_] == '0') {
            ++at_;
        } else if (at_ < n && digit(s_[at_])) {
            while (at_ < n && digit(s_[at_])) {
                ++at_;
            }
        } else {
            return ParseCode::syntax;
        }
        const std::size_t last = at_;
        bool floating = false;
        if (at_ + 1 < n && s_[at_] == '.' && digit(s_[at_ + 1])) {
            at_ += 2;
            while (at_ < n && digit(s_[at_])) {
                ++at_;
            }
            floating = true;
        }
        if (at_ < n && (s_[at_] == 'e' || s_[at_] == 'E')) {
            std::size_t j = at_ + 1;
            j += j < n && (s_[j] == '+' || s_[j] == '-') ? 1U : 0U;
            if (j < n && digit(s_[j])) {
                while (j < n && digit(s_[j])) {
                    ++j;
                }
                at_ = j;
                floating = true;
            }
        }
        if (floating) {
            return ParseCode::floating;
        }
        if (last - first > max_digits) {
            return ParseCode::int_range;
        }
        std::int64_t v = 0;
        for (std::size_t i = first; i < last; ++i) {
            v = v * 10 + (s_[i] - '0');
        }
        if (v > max_integer) {
            return ParseCode::int_range;
        }
        node();
        if (w_ != nullptr) {
            w_->integer(negative ? -v : v);
        }
        return std::nullopt;
    }
    // A string, a value or a key: a raw control character, an unknown
    // escape or a short \u is syntax; a \u high surrogate and a \u low one
    // after it are one code point, any other surrogate kept alone.
    constexpr std::optional<ParseCode> string() {
        ++at_;
        if (w_ != nullptr) {
            w_->begin();
        }
        while (true) {
            if (at_ >= s_.size()) {
                return ParseCode::syntax;
            }
            const char c = s_[at_];
            if (c == '"') {
                ++at_;
                break;
            }
            if (static_cast<unsigned char>(c) < 0x20U) {
                return ParseCode::syntax;
            }
            if (c != '\\') {
                put(c);
                ++at_;
                continue;
            }
            if (at_ + 1 >= s_.size()) {
                return ParseCode::syntax;
            }
            const char e = s_[at_ + 1];
            at_ += 2;
            const std::string_view from = "\"\\/bfnrt";
            const std::string_view to = "\"\\/\b\f\n\r\t";
            if (const std::size_t k = from.find(e); k != std::string_view::npos) {
                put(to[k]);
                continue;
            }
            if (e != 'u') {
                return ParseCode::syntax;
            }
            const std::optional<char32_t> high = hex4(s_, at_);
            if (!high) {
                return ParseCode::syntax;
            }
            at_ += 4;
            char32_t point = *high;
            if (point >= 0xD800U && point <= 0xDBFFU && ahead("\\u")) {
                const std::optional<char32_t> low = hex4(s_, at_ + 2);
                if (low && *low >= 0xDC00U && *low <= 0xDFFFU) {
                    point = 0x10000U + ((point - 0xD800U) << 10U) + (*low - 0xDC00U);
                    at_ += 6;
                }
            }
            put_point(point);
        }
        node();
        if (w_ != nullptr) {
            w_->end();
        }
        return std::nullopt;
    }

    std::string_view s_;
    std::size_t limit_;
    Writer<A>* w_;
    const Basic<A>* value_;
    Keys* keys_;
    std::size_t at_ = 0;
    std::size_t open_ = 0;
    std::array<Kind, max_depth> kinds_{};
    std::array<std::size_t, max_depth> members_{};
    std::array<Index, max_depth> starts_{};
    Tally tally_;
};

}  // namespace detail

// Steps 3 and 4 into out, which is replaced only on success: depth past
// depth levels (at most 16), then the pass's first fault, or none. An input
// past max_input bytes is MemoryError budget.
template <typename A>
constexpr std::optional<ParseCode> try_scan(std::string_view data, std::size_t depth, Basic<A>& out) {
    if (depth > max_depth) {
        memory::fail_fast("json::scan: a depth past 16");
    }
    if (data.size() > max_input) {
        too_large(max_input, data.size(), 0);
    }
    if (detail::too_deep(data, depth)) {
        return ParseCode::depth;
    }
    detail::PassView<A> count(data, depth, nullptr, nullptr, nullptr);
    const std::optional<ParseCode> first = count.run();
    const detail::Tally& t = count.tally();
    if (first && t.members < 2) {
        return first;  // no object closed that could hold a repeated key
    }
    Basic<A> value(out.allocator());
    value.reserve(t.nodes, t.text);
    typename detail::PassView<A>::Keys keys(out.allocator());
    keys.reserve(t.members);
    Writer<A> writer(value);
    detail::PassView<A> build(data, depth, &writer, &value, &keys);
    if (const std::optional<ParseCode> fault = build.run()) {
        return fault;
    }
    out = std::move(value);
    return std::nullopt;
}

// try_scan, a fault thrown as ParseError.
template <typename A>
constexpr void scan(std::string_view data, std::size_t depth, Basic<A>& out) {
    if (const std::optional<ParseCode> fault = try_scan(data, depth, out)) {
        throw ParseError{*fault};
    }
}

}  // namespace qiven3::json
