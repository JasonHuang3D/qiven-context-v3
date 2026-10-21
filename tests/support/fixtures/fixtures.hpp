#pragma once

// fixtures: the files of tests/fixtures/, loaded through canon::parse.
// Configure compiles each file into the binary as its bytes
// (support/fixtures/CMakeLists.txt), so that a case reads no path and
// makes no platform call; load parses a file's bytes strictly, as canon
// does any input, into the caller's value, which is replaced only on
// success. entries.json is one object: "entries", each entry by its name
// with its key in hex, its content's canonical bytes and its stored header
// line, key and content both absent on an entry that has none; and
// "files", the versions of a mapped file, each with its repository, its
// path, its version and its bytes in hex. load_entries checks that layout
// too, every member of every item, and refuses any other member. Loading
// takes canon's allocations alone, four, within canon's budget; the
// layout check, the accessors and key_bytes take none, and their views
// borrow the value's text; from_hex at most one, its result's. Each but
// bytes is constexpr and a template on the allocator or the string type,
// so its vectors run in constant evaluation too; it reads by index, with
// no pointer arithmetic.
//
// thread: none

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "memory/fail.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::test::fixtures {

namespace json = qiven3::json;
namespace canon = qiven3::canon;

// The compiled-in bytes of tests/fixtures/<file>, or none for a name the
// build lacks; the files the build holds, by index, in name order.
std::optional<std::string_view> bytes(std::string_view file) noexcept;
std::size_t file_count() noexcept;
std::string_view file_name(std::size_t i) noexcept;

// A load's own faults: a file the build lacks, or an item whose layout is
// not entries.json's (the root, entry i or file i). The parser's are its
// ParseError.
enum class Code : std::uint8_t { no_file, root, entry, file };
struct LoadError {
    Code code;
    std::size_t item;
};

// An entry: views of its loaded value's text.
struct EntryView {
    std::string_view name;
    std::optional<std::string_view> key;
    std::optional<std::string_view> content;
    std::string_view header;
};
static_assert(sizeof(EntryView) == 80, "EntryView's size is pinned: two views and two optional views");

// A version of a mapped file: its bytes as hex.
struct FileView {
    std::string_view repository;
    std::string_view path;
    std::int64_t version = 0;
    std::string_view hex;
};
static_assert(sizeof(FileView) == 56, "FileView's size is pinned: three views and the version");

inline constexpr std::size_t key_bytes_size = 32;
using KeyBytes = std::array<std::byte, key_bytes_size>;

namespace detail {

constexpr bool hex_digit(char c) noexcept { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

constexpr bool hex(std::string_view text) noexcept {
    if (text.size() % 2 != 0) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!hex_digit(text[i])) {
            return false;
        }
    }
    return true;
}

constexpr std::uint8_t nibble(char c) noexcept {
    return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
}

constexpr std::uint8_t octet(std::string_view text, std::size_t i) noexcept {
    return static_cast<std::uint8_t>((nibble(text[2 * i]) << 4U) | nibble(text[(2 * i) + 1]));
}

// Whether node at is an object whose every member is named in names and
// which holds the first want of them; the members' values are the
// caller's to check.
template <typename A, std::size_t N>
constexpr bool members(const json::Basic<A>& v, json::Index at, const std::array<std::string_view, N>& names,
                       std::size_t want) {
    if (v.kind(at) != json::Kind::object) {
        return false;
    }
    std::size_t seen = 0;
    for (json::Index k = at + 1; k < v.next(at); k = v.next(k + 1)) {
        bool known = false;
        for (std::size_t n = 0; n < N; ++n) {
            known = known || v.text(k) == names[n];
        }
        if (!known) {
            return false;
        }
        for (std::size_t n = 0; n < want; ++n) {
            seen += v.text(k) == names[n] ? 1U : 0U;
        }
    }
    return seen == want;
}

template <typename A>
constexpr bool string_at(const json::Basic<A>& v, std::optional<json::Index> at) {
    return !at || v.kind(*at) == json::Kind::string;
}

// The items of array at, by index: item i's node.
template <typename A>
constexpr json::Index item(const json::Basic<A>& v, json::Index at, std::size_t i) {
    if (v.kind(at) != json::Kind::array || i >= v.count(at)) {
        memory::fail_fast("fixtures: an item past its array");
    }
    json::Index k = at + 1;
    for (std::size_t n = 0; n < i; ++n) {
        k = v.next(k);
    }
    return k;
}

template <typename A>
constexpr json::Index array_of(const json::Basic<A>& v, std::string_view name) {
    const std::optional<json::Index> at = v.find(0, name);
    if (!at || v.kind(*at) != json::Kind::array) {
        memory::fail_fast("fixtures: a value of no entries.json layout");
    }
    return *at;
}

template <typename A>
constexpr std::optional<LoadError> entry_layout(const json::Basic<A>& v, json::Index entries, std::size_t i) {
    const json::Index at = item(v, entries, i);
    // name and header first: the two each entry holds.
    constexpr std::array<std::string_view, 4> names{"name", "header", "key", "content"};
    if (!members(v, at, names, 2)) {
        return LoadError{Code::entry, i};
    }
    const std::optional<json::Index> key = v.find(at, "key");
    const std::optional<json::Index> content = v.find(at, "content");
    if (!string_at(v, v.find(at, "name")) || !string_at(v, v.find(at, "header")) || !string_at(v, key) ||
        !string_at(v, content) || key.has_value() != content.has_value() ||
        (key && (v.text(*key).size() != 2 * key_bytes_size || !hex(v.text(*key))))) {
        return LoadError{Code::entry, i};
    }
    for (std::size_t j = 0; j < i; ++j) {
        const json::Index other = item(v, entries, j);
        if (v.text(*v.find(other, "name")) == v.text(*v.find(at, "name"))) {
            return LoadError{Code::entry, i};
        }
    }
    return std::nullopt;
}

template <typename A>
constexpr std::optional<LoadError> file_layout(const json::Basic<A>& v, json::Index at, std::size_t i) {
    constexpr std::array<std::string_view, 4> names{"bytes", "path", "repository", "version"};
    if (!members(v, at, names, 4)) {
        return LoadError{Code::file, i};
    }
    const json::Index version = *v.find(at, "version");
    if (!string_at(v, v.find(at, "bytes")) || !string_at(v, v.find(at, "path")) ||
        !string_at(v, v.find(at, "repository")) || v.kind(version) != json::Kind::integer ||
        v.integer(version) < 1 || !hex(v.text(*v.find(at, "bytes")))) {
        return LoadError{Code::file, i};
    }
    return std::nullopt;
}

}  // namespace detail

// The first item, in document order, whose layout is not entries.json's,
// or none.
template <typename A>
constexpr std::optional<LoadError> layout(const json::Basic<A>& v) {
    constexpr std::array<std::string_view, 2> names{"entries", "files"};
    if (v.empty() || !detail::members(v, 0, names, 2) || v.kind(*v.find(0, "entries")) != json::Kind::array ||
        v.kind(*v.find(0, "files")) != json::Kind::array) {
        return LoadError{Code::root, 0};
    }
    const json::Index entries = *v.find(0, "entries");
    for (std::size_t i = 0; i < v.count(entries); ++i) {
        if (const std::optional<LoadError> fault = detail::entry_layout(v, entries, i)) {
            return fault;
        }
    }
    const json::Index files = *v.find(0, "files");
    for (std::size_t i = 0; i < v.count(files); ++i) {
        if (const std::optional<LoadError> fault = detail::file_layout(v, detail::item(v, files, i), i)) {
            return fault;
        }
    }
    return std::nullopt;
}

// data's value into out, parsed strictly and its layout checked: canon's
// ParseError, or LoadError for the layout; out replaced only on success.
template <typename A>
constexpr void parse_entries(std::string_view data, json::Basic<A>& out) {
    json::Basic<A> value(out.allocator());
    canon::parse(data, value);
    if (const std::optional<LoadError> fault = layout(value)) {
        throw *fault;
    }
    out = std::move(value);
}

// tests/fixtures/<file> into out through canon::parse: LoadError no_file
// for a name the build lacks, else canon's faults.
template <typename A>
void load(std::string_view file, json::Basic<A>& out) {
    const std::optional<std::string_view> data = bytes(file);
    if (!data) {
        throw LoadError{Code::no_file, 0};
    }
    canon::parse(*data, out);
}

// tests/fixtures/entries.json into out, its layout checked.
template <typename A>
void load_entries(json::Basic<A>& out) {
    const std::optional<std::string_view> data = bytes("entries.json");
    if (!data) {
        throw LoadError{Code::no_file, 0};
    }
    parse_entries(*data, out);
}

// A loaded entries.json's entries and files, by index or by name; an
// index past the count ends the process.
template <typename A>
constexpr std::size_t entry_count(const json::Basic<A>& v) {
    return v.count(detail::array_of(v, "entries"));
}

template <typename A>
constexpr EntryView entry(const json::Basic<A>& v, std::size_t i) {
    const json::Index at = detail::item(v, detail::array_of(v, "entries"), i);
    EntryView e{v.text(*v.find(at, "name")), std::nullopt, std::nullopt, v.text(*v.find(at, "header"))};
    if (const std::optional<json::Index> key = v.find(at, "key")) {
        e.key = v.text(*key);
    }
    if (const std::optional<json::Index> content = v.find(at, "content")) {
        e.content = v.text(*content);
    }
    return e;
}

template <typename A>
constexpr std::optional<EntryView> find_entry(const json::Basic<A>& v, std::string_view name) {
    for (std::size_t i = 0; i < entry_count(v); ++i) {
        const EntryView e = entry(v, i);
        if (e.name == name) {
            return e;
        }
    }
    return std::nullopt;
}

template <typename A>
constexpr std::size_t mapped_count(const json::Basic<A>& v) {
    return v.count(detail::array_of(v, "files"));
}

template <typename A>
constexpr FileView mapped(const json::Basic<A>& v, std::size_t i) {
    const json::Index at = detail::item(v, detail::array_of(v, "files"), i);
    return FileView{v.text(*v.find(at, "repository")), v.text(*v.find(at, "path")),
                    v.integer(*v.find(at, "version")), v.text(*v.find(at, "bytes"))};
}

// A key's 64 lowercase hex digits as its 32 bytes, or none.
constexpr std::optional<KeyBytes> key_bytes(std::string_view hex) noexcept {
    if (hex.size() != 2 * key_bytes_size || !detail::hex(hex)) {
        return std::nullopt;
    }
    KeyBytes key{};
    for (std::size_t i = 0; i < key_bytes_size; ++i) {
        key[i] = static_cast<std::byte>(detail::octet(hex, i));
    }
    return key;
}

// Lowercase hex's bytes into out, replaced only on success: at most one
// allocation, the result's; false, out unchanged, for an odd length or another digit.
template <typename S>
constexpr bool from_hex(std::string_view hex, S& out) {
    if (!detail::hex(hex)) {
        return false;
    }
    S bytes_out(out.get_allocator());
    bytes_out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size() / 2; ++i) {
        bytes_out.push_back(static_cast<char>(detail::octet(hex, i)));
    }
    out = std::move(bytes_out);
    return true;
}

}  // namespace qiven3::test::fixtures
