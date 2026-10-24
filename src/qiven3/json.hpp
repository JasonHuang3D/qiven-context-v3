#pragma once

// json: the parser's grammar and the canonical encoder, behind canon. A
// value is a tape: its nodes in document order, 8 bytes each, a container
// holding its count and the index past its last node, and its strings'
// bytes in one text beside them. scan reads text in three passes, none
// recursive: the depth scan as written, a pass that counts what the value
// takes, then the pass that builds it in that much, so a scan takes one
// allocation for the nodes, one for the text and one for the duplicate
// keys' check. encode measures its bytes, then writes them in one
// allocation, keys in code point order. Each function is constexpr and a
// template on the allocator, so its vectors run in constant evaluation too;
// it reads by index, with no pointer arithmetic.
//
// thread: one (a value stays in the thread that made it)

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::json {

enum class Kind : std::uint8_t { null, boolean, integer, string, array, object };

using Index = std::uint32_t;

// The text's limits: 16 levels, the outermost level 1; integers within
// plus or minus 2^53 - 1, at most 16 digits read.
inline constexpr std::size_t max_depth = 16;
inline constexpr std::int64_t max_integer = 9007199254740991;
inline constexpr std::size_t max_digits = 16;
// The tape's limits: an input past max_input bytes, or a value past these,
// is MemoryError budget.
inline constexpr std::size_t max_input = (std::size_t{1} << 30U) - 1U;
inline constexpr std::size_t max_nodes = (std::size_t{1} << 30U) - 1U;
inline constexpr std::size_t max_text = (std::size_t{1} << 30U) - 1U;

// One node: its kind in bits 0 to 2; a boolean in bit 3; an integer in bits
// 3 to 63; a string's offset in bits 3 to 33 and length in 34 to 63; a
// container's count (items, or members) in bits 3 to 33 and the index past
// its last node in 34 to 63.
class Node {
public:
    static constexpr Node of(Kind kind, std::uint64_t low = 0, std::uint64_t high = 0) noexcept {
        return Node{static_cast<std::uint64_t>(kind) | (low << 3U) | (high << 34U)};
    }
    static constexpr Node of_integer(std::int64_t value) noexcept {
        return Node{(static_cast<std::uint64_t>(value) << 3U) | static_cast<std::uint64_t>(Kind::integer)};
    }
    [[nodiscard]] constexpr Kind kind() const noexcept { return static_cast<Kind>(word_ & 7U); }
    // The tag's bits cleared leave a multiple of 8, which the division
    // takes back to of_integer's value, its sign kept.
    [[nodiscard]] constexpr std::int64_t integer() const noexcept {
        return static_cast<std::int64_t>(word_ & ~std::uint64_t{7}) / 8;
    }
    [[nodiscard]] constexpr std::size_t low() const noexcept { return (word_ >> 3U) & 0x7FFFFFFFU; }
    [[nodiscard]] constexpr std::size_t high() const noexcept { return static_cast<std::size_t>(word_ >> 34U); }
    constexpr bool operator==(const Node&) const = default;

private:
    constexpr explicit Node(std::uint64_t word) noexcept : word_(word) {}
    std::uint64_t word_ = 0;
};

static_assert(sizeof(Node) == 8, "Node's size is pinned: one word");

[[noreturn]] inline void too_large(std::size_t limit, std::size_t request, std::size_t held) {
    throw memory::MemoryError::budget("json", limit, request, held);
}

template <typename A>
class Writer;

// A value: empty, or its root at index 0 and each node's children after it.
// Reading a node as another kind is a caller's mistake: fail_fast.
template <typename A = std::allocator<char>>
class Basic {
public:
    using allocator_type = A;
    using Nodes = std::vector<Node, typename std::allocator_traits<A>::template rebind_alloc<Node>>;
    using Text = std::basic_string<char, std::char_traits<char>, A>;

    constexpr explicit Basic(const A& allocator = A()) noexcept : nodes_(allocator), text_(allocator) {}

    [[nodiscard]] constexpr A allocator() const noexcept { return text_.get_allocator(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return nodes_.empty(); }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return nodes_.size(); }
    [[nodiscard]] constexpr std::size_t text_size() const noexcept { return text_.size(); }
    // Room for this many nodes and text bytes, each taken in one allocation.
    constexpr void reserve(std::size_t nodes, std::size_t text) {
        nodes_.reserve(nodes);
        text_.reserve(text);
    }

    [[nodiscard]] constexpr Kind kind(Index i = 0) const { return nodes_.at(i).kind(); }
    [[nodiscard]] constexpr bool boolean(Index i = 0) const { return at(i, Kind::boolean).low() != 0; }
    [[nodiscard]] constexpr std::int64_t integer(Index i = 0) const { return at(i, Kind::integer).integer(); }
    [[nodiscard]] constexpr std::string_view text(Index i = 0) const {
        const Node n = at(i, Kind::string);
        return std::string_view(text_).substr(n.low(), n.high());
    }
    // A container's items, or members.
    [[nodiscard]] constexpr std::size_t count(Index i = 0) const {
        const Node n = nodes_.at(i);
        if (n.kind() != Kind::array && n.kind() != Kind::object) {
            memory::fail_fast("json: a count of no container");
        }
        return n.low();
    }
    // The index past node i's last node: its next sibling's. A container's
    // children run from i + 1 to it; an object's are key, value, key, ...
    [[nodiscard]] constexpr Index next(Index i) const {
        const Node n = nodes_.at(i);
        return n.kind() == Kind::array || n.kind() == Kind::object ? static_cast<Index>(n.high()) : i + 1;
    }
    // The value of an object's member, or none.
    [[nodiscard]] constexpr std::optional<Index> find(Index object, std::string_view key) const {
        static_cast<void>(at(object, Kind::object));
        const Index end = next(object);
        for (Index k = object + 1; k < end; k = next(k + 1)) {
            if (text(k) == key) {
                return k + 1;
            }
        }
        return std::nullopt;
    }
    // The levels of arrays and objects in node i: 0 for a scalar, 1 for
    // an empty container.
    [[nodiscard]] constexpr std::size_t depth(Index i = 0) const {
        std::array<Index, max_depth> ends{};
        std::size_t open = 0;
        std::size_t most = 0;
        for (Index k = i; k < next(i); ++k) {
            while (open > 0 && ends.at(open - 1) == k) {
                --open;
            }
            if (kind(k) == Kind::array || kind(k) == Kind::object) {
                ends.at(open++) = next(k);
                most = std::max(most, open);
            }
        }
        return most;
    }

    // A member appended to the root object, or an item to the root array;
    // the root takes one level, so value's depth stays within 15.
    constexpr void set(std::string_view key, std::string_view value) { add_member(key, of(value)); }
    constexpr void set(std::string_view key, std::int64_t value) { add_member(key, of(value)); }
    template <std::same_as<bool> B>
    constexpr void set(std::string_view key, B value) {
        add_member(key, of(value));
    }
    template <typename C>
    constexpr void set(std::string_view key, const Basic<C>& value) {
        add_member(key, value);
    }
    template <typename C>
    constexpr void push(const Basic<C>& value) {
        open_root(Kind::array);
        append(value, 1);
        close_root();
    }

    constexpr bool operator==(const Basic&) const = default;

private:
    template <typename B>
    friend class Writer;
    template <typename B>
    friend class Basic;

    [[nodiscard]] constexpr Node at(Index i, Kind kind) const {
        const Node n = nodes_.at(i);
        if (n.kind() != kind) {
            memory::fail_fast("json: a node read as another kind");
        }
        return n;
    }
    template <typename T>
    constexpr Basic of(const T& scalar) const;
    template <typename C>
    constexpr void add_member(std::string_view key, const Basic<C>& value) {
        open_root(Kind::object);
        put(Node::of(Kind::string, put_text(key), key.size()));
        append(value, 1);
        close_root();
    }
    constexpr void open_root(Kind kind) {
        if (nodes_.empty()) {
            put(Node::of(kind));
        }
        if (nodes_.front().kind() != kind) {
            memory::fail_fast("json: an append to a root of another kind");
        }
    }
    constexpr void close_root() {
        const std::size_t items = nodes_.front().low() + 1;
        nodes_.front() = Node::of(nodes_.front().kind(), items, nodes_.size());
    }
    constexpr void put(Node n) {
        if (nodes_.size() >= max_nodes) {
            too_large(max_nodes, nodes_.size() + 1, nodes_.size());
        }
        nodes_.push_back(n);
    }
    constexpr std::size_t put_text(std::string_view bytes) {
        if (bytes.size() > max_text - text_.size()) {
            too_large(max_text, bytes.size(), text_.size());
        }
        const std::size_t offset = text_.size();
        text_.append(bytes);
        return offset;
    }
    // Another value's nodes appended below `levels` open levels, its
    // strings' offsets and its containers' ends moved to this tape.
    template <typename C>
    constexpr void append(const Basic<C>& other, std::size_t levels) {
        if (other.empty() || levels + other.depth() > max_depth) {
            memory::fail_fast("json: an empty value, or one past 16 levels, appended");
        }
        if (other.size() > max_nodes - nodes_.size()) {
            too_large(max_nodes, other.size(), nodes_.size());
        }
        const std::size_t base = nodes_.size();
        const std::size_t shift = put_text(other.text_);
        nodes_.reserve(base + other.size());
        for (const Node n : other.nodes_) {
            switch (n.kind()) {
            case Kind::string: nodes_.push_back(Node::of(Kind::string, n.low() + shift, n.high())); break;
            case Kind::array:
            case Kind::object: nodes_.push_back(Node::of(n.kind(), n.low(), n.high() + base)); break;
            default: nodes_.push_back(n); break;
            }
        }
    }

    Nodes nodes_;
    Text text_;
};

// Builds an empty value in document order: an object's children key,
// value, key, ... At most 16 levels open; a misuse is fail_fast.
template <typename A>
class Writer {
public:
    constexpr explicit Writer(Basic<A>& value) : value_(&value) {
        if (!value.empty()) {
            memory::fail_fast("json::Writer: a value not empty");
        }
    }
    constexpr void null() { scalar(Node::of(Kind::null)); }
    constexpr void boolean(bool b) { scalar(Node::of(Kind::boolean, b ? 1U : 0U)); }
    constexpr void integer(std::int64_t v) { scalar(Node::of_integer(v)); }
    constexpr void string(std::string_view bytes) {
        scalar(Node::of(Kind::string, value_->put_text(bytes), bytes.size()));
    }
    // A string written in parts: begin, then bytes and code points, then
    // end, which makes its node.
    constexpr void begin() { start_ = value_->text_.size(); }
    constexpr void byte(char c) {
        if (value_->text_.size() >= max_text) {
            too_large(max_text, 1, value_->text_.size());
        }
        value_->text_.push_back(c);
    }
    constexpr void point(char32_t p) {
        if (value_->text_.size() + 4 > max_text) {
            too_large(max_text, 4, value_->text_.size());
        }
        utf::append(value_->text_, p);
    }
    constexpr void end() { scalar(Node::of(Kind::string, start_, value_->text_.size() - start_)); }
    constexpr void open(Kind kind) {
        if (open_ == max_depth) {
            memory::fail_fast("json::Writer: a 17th level");
        }
        scalar(Node::of(kind));
        starts_.at(open_) = static_cast<Index>(value_->nodes_.size() - 1);
        counts_.at(open_) = 0;
        ++open_;
    }
    constexpr void close() {
        if (open_ == 0) {
            memory::fail_fast("json::Writer: a close with no level open");
        }
        --open_;
        Node& n = value_->nodes_.at(starts_.at(open_));
        const std::size_t children = counts_.at(open_);
        n = Node::of(n.kind(), n.kind() == Kind::object ? children / 2 : children, value_->nodes_.size());
    }
    // Another whole value as one child.
    template <typename C>
    constexpr void value(const Basic<C>& other) {
        value_->append(other, open_);
        counted();
    }
    [[nodiscard]] constexpr std::size_t levels() const noexcept { return open_; }

private:
    constexpr void scalar(Node n) {
        value_->put(n);
        counted();
    }
    constexpr void counted() {
        if (open_ > 0) {
            ++counts_.at(open_ - 1);
        }
    }

    Basic<A>* value_;
    std::array<Index, max_depth> starts_{};
    std::array<std::size_t, max_depth> counts_{};
    std::size_t open_ = 0;
    std::size_t start_ = 0;
};

template <typename A>
template <typename T>
constexpr Basic<A> Basic<A>::of(const T& scalar) const {
    Basic<A> one(allocator());
    Writer<A> w(one);
    if constexpr (std::same_as<T, bool>) {
        w.boolean(scalar);
    } else if constexpr (std::same_as<T, std::int64_t>) {
        w.integer(scalar);
    } else {
        w.string(scalar);
    }
    return one;
}

using Value = Basic<memory::Allocator<char>>;

}  // namespace qiven3::json

#include "qiven3/json_bytes.hpp"

