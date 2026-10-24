#pragma once

// secret: the secret check, the twelve pinned patterns over every string of
// an entry. check(header, content) reads the header's present text fields
// in their fixed order (tag to content; seq, an integer, and the line's id,
// which hashes the header, are no field it reads), each against PATTERNS in
// order: the first hit is refused, SecretError carrying the pattern and the
// field's path, header.scope, and never the value, a part, a length or a
// digest of it. A header hit is never cleared. Then each string value of
// the content, in the value's order, each against PATTERNS in order: every
// hit returned, a pattern and the string's path, such as
// evidence[0].command, the caller to settle them. The patterns are known
// formats, never entropy, matched as Python's re.search matches them by
// matcher's automata.
//
// Readings: the content is the value part 1 admitted, parsed from its
// canonical bytes, so its order is canonical; a value in another order is
// read in its own. Keys are no strings it reads: part 1 admits only the
// names kinds' tables define. A path is held by value, cut at
// content::path_size, as content's faults are.
//
// Allocations: the twelve compiles, two each, a scratch freed before the
// next and its automaton held until the check returns: 24; then the hits,
// none while there is none, one at the first (room for first_hits) and one
// each time their count passes the room, which doubles. A header hit takes
// the 24 alone and holds nothing once thrown. A MemoryError leaves nothing
// held and the caller's hits unchanged. The peak: the twelve automata, a
// scratch and the hits' room. Each function is constexpr and a template on
// the allocator, so its vectors run in constant evaluation too; it reads by
// index, with no pointer arithmetic, and never recurses. secret logs
// nothing.
//
// thread: none

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/content_fields.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/matcher.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::secret {

// A pattern's text, held by value: a literal's characters, at most 239.
struct Text {
    std::array<char, 239> bytes{};
    std::uint8_t size = 0;

    constexpr Text() noexcept = default;
    // Implicit, as kinds' Label is; a literal past 239 characters does not
    // compile.
    consteval Text(const char* literal) noexcept {
        const std::string_view s(literal);
        if (s.size() > bytes.size()) {
            memory::fail_fast("secret::Text: past 239 characters");
        }
        for (std::size_t i = 0; i < s.size(); ++i) {
            bytes[i] = s[i];
        }
        size = static_cast<std::uint8_t>(s.size());
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept { return std::string_view(bytes.data(), size); }
};
static_assert(sizeof(Text) == 240, "Text's size is pinned: 239 characters and their count");

// A pattern: its name and its text in Python's re.
struct Pattern {
    kinds::Label name;
    Text text;
};
static_assert(sizeof(Pattern) == 256, "Pattern's size is pinned: its label and its text");

// The twelve, in the table's order: the first nine, then the three the
// prior art lends. In the first two of those, a value that is a variable
// or a placeholder misses.
inline constexpr std::array<Pattern, 12> PATTERNS = {{
    {"private-key", R"(-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY(?: BLOCK)?-----)"},
    {"github-ghp", R"((?<![A-Za-z0-9_])ghp_[A-Za-z0-9]{36})"},
    {"github-pat", R"((?<![A-Za-z0-9_])github_pat_[A-Za-z0-9_]{82})"},
    {"sk-key", R"((?<![A-Za-z0-9_-])sk-[A-Za-z0-9_-]{32,})"},
    {"aws-akia", R"((?<![A-Z0-9])AKIA[A-Z0-9]{16})"},
    {"slack-xox", R"((?<![A-Za-z0-9])xox[abceoprs]-[0-9]+-[A-Za-z0-9-]{8,})"},
    {"slack-xoxd", R"((?<![A-Za-z0-9])xoxd-[A-Za-z0-9%+/=]{32,})"},
    {"url-password", R"([A-Za-z][A-Za-z0-9+.-]*://[^ \t\r\n/?#@:]*:[^ \t\r\n/?#@]+@)"},
    {"pair-password",
     R"((?<![A-Za-z0-9])(?:PG)?[Pp](?:[Aa][Ss][Ss])?[Ww](?:[Oo][Rr])?[Dd][ \t]*=[ \t]*["']?[^ \t\r\n;&'"]+)"},
    {"auth-header",
     R"((?<![A-Za-z0-9_])[Aa][Uu][Tt][Hh][Oo][Rr][Ii][Zz][Aa][Tt][Ii][Oo][Nn]["']?[ \t]*:[ \t]*["']?)"
     R"((?:[Bb][Ee][Aa][Rr][Ee][Rr]|[Bb][Aa][Ss][Ii][Cc]|[Tt][Oo][Kk][Ee][Nn]|[Aa][Pp][Ii][Kk][Ee][Yy]))"
     R"([ \t]+[A-Za-z0-9._~+/=-]{8,})"},
    {"cli-secret",
     R"((?<![A-Za-z0-9_-])--(?:password|token|api-key|secret)(?:[ \t]+|[ \t]*=[ \t]*)["']?[^ \t\r\n"'$<%{-])"},
    {"tencent-akid", R"((?<![A-Za-z0-9])AKID[A-Za-z0-9]{32})"},
}};

namespace detail {

// Whether PATTERNS' names are content's, which a direction's clears names,
// in the same order.
consteval bool same_names() {
    for (std::size_t i = 0; i < PATTERNS.size(); ++i) {
        if (PATTERNS.at(i).name.view() != content::pattern_names.at(i)) {
            return false;
        }
    }
    return PATTERNS.size() == content::pattern_names.size();
}

}  // namespace detail
static_assert(detail::same_names(), "PATTERNS' names are content's pattern_names, in order");

// A pattern's name.
constexpr std::string_view name(std::uint8_t pattern) { return PATTERNS.at(pattern).name.view(); }

// A hit: the pattern's index in PATTERNS and the string's path, by value.
struct Hit {
    std::uint8_t pattern = 0;
    memory::Inline<content::path_size> path;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Hit) == 112, "Hit's size is pinned: its pattern and its path");
#endif

// A header hit refused: the pattern and the field's path; never the value.
struct SecretError {
    std::uint8_t pattern = 0;
    memory::Inline<content::path_size> path;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(SecretError) == 112, "SecretError's size is pinned: its pattern and its path");
#endif

template <typename A>
using Hits = std::vector<Hit, typename std::allocator_traits<A>::template rebind_alloc<Hit>>;

// The hits' room at the first; it doubles each time it fills.
inline constexpr std::size_t first_hits = 4;

namespace detail {

template <typename A>
using Automata = std::array<matcher::Basic<A>, PATTERNS.size()>;

template <typename A, std::size_t... I>
constexpr Automata<A> fresh(const A& allocator, std::index_sequence<I...> /*each*/) noexcept {
    return {((void)I, matcher::Basic<A>(allocator))...};
}

// The twelve compiled, in order: 24 allocations, 12 held.
template <typename A>
constexpr Automata<A> compile_all(const A& allocator) {
    Automata<A> out = fresh(allocator, std::make_index_sequence<PATTERNS.size()>{});
    for (std::size_t p = 0; p < PATTERNS.size(); ++p) {
        if (matcher::try_compile(PATTERNS.at(p).text.view(), out.at(p))) {
            memory::fail_fast("secret: a pinned pattern the matcher refuses");
        }
    }
    return out;
}

// The first pattern that hits text, or none.
template <typename A>
constexpr std::optional<std::uint8_t> first(const Automata<A>& automata, std::string_view text) {
    for (std::size_t p = 0; p < automata.size(); ++p) {
        if (automata.at(p).hit(text)) {
            return static_cast<std::uint8_t>(p);
        }
    }
    return std::nullopt;
}

// A hit appended, the room doubled when full.
template <typename H>
constexpr void push(H& hits, std::uint8_t pattern, std::string_view path) {
    if (hits.size() == hits.capacity()) {
        const std::optional<std::size_t> room = hits.capacity() == 0 ? first_hits : memory::mul(hits.capacity(), 2);
        if (!room) {
            memory::fail_fast("secret: the hits' room past std::size_t");
        }
        hits.reserve(*room);
    }
    hits.push_back(Hit{pattern, memory::Inline<content::path_size>(path)});
}

// An open container of the content: where it ends, its path's length and
// its next item's index.
struct Level {
    json::Index end = 0;
    std::uint32_t item = 0;
    std::size_t cut = 0;
    bool object = false;
};

// Each string value of a content, in the value's order, to visit with
// its path.
template <typename B, typename Visit>
constexpr void strings(const canon::Basic<B>& value, const Visit& visit) {
    if (value.empty()) {
        return;
    }
    content::detail::PathText path;
    std::array<Level, json::max_depth + 1> open{};
    std::size_t depth = 0;
    const json::Index end = value.next(0);
    json::Index k = 0;
    while (k < end) {
        while (depth > 0 && k >= open.at(depth - 1).end) {
            --depth;
        }
        if (depth > 0) {
            Level& level = open.at(depth - 1);
            path.cut(level.cut);
            if (level.object) {
                path.key(value.text(k));
                ++k;
            } else {
                path.item(level.item);
                ++level.item;
            }
        }
        const json::Kind kind = value.kind(k);
        if (kind == json::Kind::string) {
            visit(value.text(k), path.view());
        } else if (kind == json::Kind::array || kind == json::Kind::object) {
            if (depth == open.size()) {
                memory::fail_fast("secret: a value past json's depth");
            }
            open.at(depth) = Level{value.next(k), 0, path.size(), kind == json::Kind::object};
            ++depth;
        }
        ++k;
    }
}

}  // namespace detail

// The header's first hit as a SecretError, or none, the content's hits
// then into out, which is replaced only then; out's allocator takes the
// automata and the hits.
template <typename A, typename B, typename H>
constexpr std::optional<SecretError> try_check(const header::Header<A>& h, const canon::Basic<B>& content,
                                               std::vector<Hit, H>& out) {
    using Chars = typename std::allocator_traits<H>::template rebind_alloc<char>;
    const detail::Automata<Chars> automata = detail::compile_all(Chars(out.get_allocator()));
    for (std::size_t i = 0; i < header::field_count; ++i) {
        const auto slot = static_cast<header::Slot>(i);
        if (h.form(slot) != header::Form::text) {
            continue;
        }
        if (const std::optional<std::uint8_t> p = detail::first(automata, h.value(slot))) {
            content::detail::PathText path;
            path.key("header");
            path.key(header::slot_names.at(i));
            return SecretError{*p, memory::Inline<content::path_size>(path.view())};
        }
    }
    std::vector<Hit, H> hits(out.get_allocator());
    detail::strings(content, [&](std::string_view text, std::string_view path) {
        for (std::size_t p = 0; p < automata.size(); ++p) {
            if (automata.at(p).hit(text)) {
                detail::push(hits, static_cast<std::uint8_t>(p), path);
            }
        }
    });
    out = std::move(hits);
    return std::nullopt;
}

// The content's hits, each a pattern and a path, on the content's
// allocator; a header hit thrown as SecretError.
template <typename A, typename B>
constexpr Hits<B> check(const header::Header<A>& h, const canon::Basic<B>& content) {
    Hits<B> out(content.allocator());
    if (const std::optional<SecretError> e = try_check(h, content, out)) {
        throw *e;
    }
    return out;
}

}  // namespace qiven3::entry::secret
