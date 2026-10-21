#pragma once

// canon: canonical bytes and the strict parser, on json and utf. parse
// reads bytes in six steps: a BOM is bom; UTF-8, read strictly, utf8;
// json's depth scan and pass, depth, then syntax, float, constant,
// int_range and duplicate_key; the walk over the value in document order,
// each key's characters before its rule and the key before its value:
// null, surrogate, nonchar, key_syntax; and the comparison of the bytes
// with the value's canonical ones, not_canonical. canonical walks a value
// the same way, then writes its canonical bytes. A key's rule is a
// KeyRule, bound at compile time: Syntax by default, or a map file's
// keys by their place; Layout::any skips the comparison. Neither function
// recurses: the walk keeps the levels open in a fixed array of 16. Each is
// constexpr and a template on the allocator, so its vectors run in
// constant evaluation too; it reads by index, with no pointer arithmetic.
// canon logs nothing.
//
// thread: none

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/fail.hpp"
#include "qiven3/json.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::canon {

// A value is json's tape; canon reads and writes it.
template <typename A = std::allocator<char>>
using Basic = json::Basic<A>;
using Value = json::Value;

#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Value) == 72, "canon's Value's size is pinned: the tape's nodes, 32, and its text, 40");
#endif

// Where a key sits: the level of its object, 1 for the root's; and for
// each level from 2 to that one, outermost first, the key of the member
// whose value it is, or an empty view where an array's item is. The walk
// makes one for each key and drops it within the rule's call, which reads
// it there alone.
struct PlaceView {
    std::span<const std::string_view> keys;
    std::size_t level = 0;
};

// A key's rule: whether a key whose characters passed is allowed in its
// place. A key it refuses is key_syntax.
template <typename K>
concept KeyRule = requires(const K& rule, std::string_view key, const PlaceView& place) {
    { rule(key, place) } -> std::same_as<bool>;
};

// A key's syntax: ^[a-z][a-z0-9]*(_[a-z0-9]+)*$.
constexpr bool key_syntax(std::string_view key) noexcept {
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (key.empty() || !lower(key.front()) || key.back() == '_') {
        return false;
    }
    for (std::size_t i = 1; i < key.size(); ++i) {
        const char c = key[i];
        if (c == '_' ? key[i - 1] == '_' : !lower(c) && !digit(c)) {
            return false;
        }
    }
    return true;
}

// The default rule: the syntax, in every place.
struct Syntax {
    constexpr bool operator()(std::string_view key, const PlaceView& /*place*/) const noexcept {
        return key_syntax(key);
    }
};

static_assert(KeyRule<Syntax>);

// Whether parse compares the bytes with the value's canonical ones (step
// 6), or takes any layout, as the registry does.
enum class Layout : std::uint8_t { canonical, any };

namespace detail {

// A string's first fault in document order: utf8 (a value built in
// memory), surrogate, nonchar; or none.
constexpr std::optional<ParseCode> characters(std::string_view text) noexcept {
    for (std::size_t at = 0; at < text.size();) {
        const utf::Read r = utf::next(text, at, utf::Form::value);
        if (r.size == 0) {
            return ParseCode::utf8;
        }
        if (r.surrogate) {
            return ParseCode::surrogate;
        }
        if (utf::is_nonchar(r.point)) {
            return ParseCode::nonchar;
        }
        at += r.size;
    }
    return std::nullopt;
}

// Step 5 over the whole value, nodes in document order; a tape keeps each
// member's key in the node before its value. Integers are checked too,
// since a value built in memory may hold one past 2^53 - 1.
template <typename A, KeyRule K>
constexpr std::optional<ParseCode> walk(const json::Basic<A>& value, const K& rule) {
    struct Level {
        json::Index end = 0;
        bool object = false;
        bool key_next = false;
    };
    std::array<Level, json::max_depth> levels{};
    // holders.at(i): the key of the member whose value is level i + 1's
    // container; empty for the root and for an array's item.
    std::array<std::string_view, json::max_depth> holders{};
    std::size_t open = 0;
    for (json::Index k = 0; k < value.size(); ++k) {
        while (open > 0 && levels.at(open - 1).end == k) {
            --open;
        }
        bool key = false;
        bool member = false;
        if (open > 0 && levels.at(open - 1).object) {
            Level& up = levels.at(open - 1);
            key = up.key_next;
            member = !key;
            up.key_next = !up.key_next;
        }
        switch (value.kind(k)) {
        case json::Kind::null: return ParseCode::null;
        case json::Kind::boolean: break;
        case json::Kind::integer:
            if (value.integer(k) > json::max_integer || value.integer(k) < -json::max_integer) {
                return ParseCode::int_range;
            }
            break;
        case json::Kind::string: {
            const std::string_view text = value.text(k);
            if (const std::optional<ParseCode> fault = characters(text)) {
                return fault;
            }
            const std::span<const std::string_view> above = holders;
            if (key && !rule(text, PlaceView{above.subspan(1, open - 1), open})) {
                return ParseCode::key_syntax;
            }
            break;
        }
        case json::Kind::array:
        case json::Kind::object:
            if (open == json::max_depth) {
                memory::fail_fast("canon: a value past 16 levels");
            }
            holders.at(open) = member ? value.text(k - 1) : std::string_view{};
            levels.at(open) = Level{value.next(k), value.kind(k) == json::Kind::object, true};
            ++open;
            break;
        }
    }
    return std::nullopt;
}

// A string type for json's encode that compares what it is given with the
// bytes it was made with, holding none: step 6 in no allocation of its own.
// parse makes one over its data and drops it within its own call.
template <typename A>
class SameView {
public:
    using allocator_type = A;

    constexpr SameView(std::string_view want, const A& allocator) noexcept : want_(want), allocator_(allocator) {}

    [[nodiscard]] constexpr A get_allocator() const noexcept { return allocator_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return at_; }
    // encode states its whole size first: another is a difference.
    constexpr void reserve(std::size_t bytes) noexcept { same_ = same_ && bytes == want_.size(); }
    constexpr void push_back(char c) noexcept {
        same_ = same_ && at_ < want_.size() && want_[at_] == c;
        ++at_;
    }
    constexpr void append(std::string_view s) noexcept {
        for (const char c : s) {
            push_back(c);
        }
    }
    [[nodiscard]] constexpr bool same() const noexcept { return same_ && at_ == want_.size(); }

private:
    std::string_view want_;
    std::size_t at_ = 0;
    A allocator_;
    bool same_ = true;
};

}  // namespace detail

// The value's canonical bytes appended to out, a string type, after the
// walk: a fault is its code, with out unchanged. json's encode takes two
// allocations, the sorted members and the bytes; a MemoryError leaves out
// unchanged too. An empty value is a caller's mistake: fail_fast.
template <typename A, typename S, KeyRule K = Syntax>
constexpr std::optional<ParseCode> try_canonical(const json::Basic<A>& value, S& out, const K& rule = K{}) {
    if (const std::optional<ParseCode> fault = detail::walk(value, rule)) {
        return fault;
    }
    json::encode(value, out);
    return std::nullopt;
}

// try_canonical, a fault thrown as ParseError.
template <typename A, typename S, KeyRule K = Syntax>
constexpr void canonical(const json::Basic<A>& value, S& out, const K& rule = K{}) {
    if (const std::optional<ParseCode> fault = try_canonical(value, out, rule)) {
        throw ParseError{*fault};
    }
}

// The strict parser: data's value into out, which is replaced only on
// success; the first fault's code, or none. Steps 1 and 2 read the bytes;
// json's scan, steps 3 and 4, takes three allocations; the walk none; the
// comparison one, encode's sorted members. An input past json's max_input
// bytes is MemoryError budget before a byte is read.
template <typename A, KeyRule K = Syntax>
constexpr std::optional<ParseCode> try_parse(std::string_view data, json::Basic<A>& out, const K& rule = K{},
                                             Layout layout = Layout::canonical) {
    if (data.size() > json::max_input) {
        json::too_large(json::max_input, data.size(), 0);
    }
    if (data.starts_with("\xef\xbb\xbf")) {
        return ParseCode::bom;
    }
    if (const std::optional<ParseCode> fault = utf::check(data)) {
        return fault;
    }
    json::Basic<A> value(out.allocator());
    if (const std::optional<ParseCode> fault = json::try_scan(data, json::max_depth, value)) {
        return fault;
    }
    if (const std::optional<ParseCode> fault = detail::walk(value, rule)) {
        return fault;
    }
    if (layout == Layout::canonical) {
        detail::SameView<A> same(data, out.allocator());
        json::encode(value, same);
        if (!same.same()) {
            return ParseCode::not_canonical;
        }
    }
    out = std::move(value);
    return std::nullopt;
}

// try_parse, a fault thrown as ParseError.
template <typename A, KeyRule K = Syntax>
constexpr void parse(std::string_view data, json::Basic<A>& out, const K& rule = K{},
                     Layout layout = Layout::canonical) {
    if (const std::optional<ParseCode> fault = try_parse(data, out, rule, layout)) {
        throw ParseError{*fault};
    }
}

}  // namespace qiven3::canon
