#pragma once

// refs: references, code references and the one path rule. A reference is
// an object of a type, entry, repo, transcript or output, holding exactly
// type and its type's fields; check_reference reads one in a parsed value
// and gives the first fault in content's code order: unknown_field (a key
// its type does not define, or none does where its type is off), missing,
// type, empty, too_long (an output's command past 400 characters), then
// syntax; within a code, members in the value's order, a missing field in
// its type's order. A type absent, of another JSON type, empty or off the
// four is that code on type, after unknown_field.
//
// check_path is the one path rule, wherever a path stands: relative, /
// separators, no leading /, no empty, . or .. segment, no backslash, no
// control character, no U+2028 or U+2029, 1 to 200 characters, no line
// suffix. check_name is a qualified name's: 1 to 200 characters, no
// whitespace or control character, no line suffix. A line suffix is
// :N, :N:M, :N-M, #LN, #LN-M or #LN-LM at the end. check_subject is a
// subject's: <type>:<name>, at most 160 characters, its type one of eight,
// its name not empty, with no whitespace or control character, and no line
// suffix on the whole subject but an entry or scope one; a repo subject
// is repo:<repository>, then /<path> up to the first #, then #<name>, each
// by its own rule. Characters are code points; whitespace is the 29 code
// points of WHITESPACE; text that is no UTF-8 meets no rule.
//
// Allocations: check_path, check_name, line_suffix and the tables none;
// check_subject two for a repo subject, the repository's syntax compiled,
// else none; check_reference two for each syntax it compiles, in the
// order it meets the fields (id, repository, revision, anchor's line
// reading, digest, client, session, message), each freed before the next:
// an entry reference two, repo four to eight, transcript eight, output
// two, fewer where a fault comes first. A compile's MemoryError leaves
// nothing held. Each function is constexpr and a template on the
// allocator, so its vectors run in constant evaluation too; it reads by
// index, with no pointer arithmetic. refs logs nothing.
//
// thread: none

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "entry/header.hpp"
#include "entry/matcher.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::entry::refs {

// The codes refs gives, in content's code order.
enum class Code : std::uint8_t { unknown_field, missing, type, empty, too_long, syntax };
inline constexpr std::array<std::string_view, 6> code_names = {"unknown_field", "missing", "type",
                                                               "empty",         "too_long", "syntax"};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }

// A reference's fields; none for a key no type defines, or the whole.
enum class Field : std::uint8_t {
    type,
    id,
    repository,
    path,
    revision,
    name,
    anchor,
    digest,
    client,
    session,
    message,
    command,
    none,
};
inline constexpr std::size_t field_count = 12;
inline constexpr std::array<std::string_view, field_count> field_names = {
    "type", "id",     "repository", "path",    "revision", "name",
    "anchor", "digest", "client",   "session", "message",  "command",
};
constexpr std::string_view name(Field f) noexcept {
    return f == Field::none ? std::string_view{} : field_names[static_cast<std::size_t>(f)];
}

// A reference's fault: its code, its field, and the node it stands at: the
// member's key, or the reference itself for missing and for no object. A
// plain value, thrown by value; it allocates nothing.
struct RefError {
    Code code;
    Field field;
    json::Index at;
};
static_assert(sizeof(RefError) == 8, "RefError's size is pinned: two codes and an index");

// The four types, each its fields as bits: those it holds, those required.
enum class Type : std::uint8_t { entry, repo, transcript, output };
inline constexpr std::array<std::string_view, 4> type_names = {"entry", "repo", "transcript", "output"};
using Fields = std::uint16_t;
constexpr Fields bit(Field f) noexcept { return static_cast<Fields>(1U << static_cast<unsigned>(f)); }
// The fields' set, combined as unsigned: Fields would promote to int.
template <std::same_as<Field>... F>
constexpr Fields bits(F... f) noexcept {
    return static_cast<Fields>((0U | ... | (1U << static_cast<unsigned>(f))));
}
struct Shape {
    Fields holds;
    Fields required;
};
static_assert(sizeof(Shape) == 4, "Shape's size is pinned: two sets of fields");
inline constexpr std::array<Shape, 4> SHAPES = {
    Shape{bits(Field::type, Field::id), bits(Field::type, Field::id)},
    Shape{bits(Field::type, Field::repository, Field::path, Field::revision, Field::name, Field::anchor,
               Field::digest),
          bits(Field::type, Field::repository, Field::path, Field::digest)},
    Shape{bits(Field::type, Field::client, Field::session, Field::message, Field::digest),
          bits(Field::type, Field::client, Field::session, Field::message, Field::digest)},
    Shape{bits(Field::type, Field::command, Field::digest), bits(Field::type, Field::command, Field::digest)},
};
// Every field some type holds.
inline constexpr Fields any_type =
    static_cast<Fields>(static_cast<unsigned>(SHAPES[0].holds) | SHAPES[1].holds | SHAPES[2].holds | SHAPES[3].holds);
static_assert(any_type == (1U << field_count) - 1U, "every field is some type's");

// The limits, in characters.
inline constexpr std::size_t command_limit = 400;
inline constexpr std::size_t path_limit = 200;
inline constexpr std::size_t name_limit = 200;
inline constexpr std::size_t anchor_limit = 64;
inline constexpr std::size_t subject_limit = 160;

// The syntaxes, each the whole value; id and digest are the header's.
inline constexpr std::string_view repository_syntax = "^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$";
inline constexpr std::string_view revision_syntax = "^[0-9a-f]{40}$";
inline constexpr std::string_view message_syntax = "^[A-Za-z0-9._:-]{1,128}$";
inline constexpr std::string_view client_syntax = "^[a-z0-9][a-z0-9-]{0,31}$";
inline constexpr std::string_view session_syntax = "^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$";
// An anchor that reads as a line.
inline constexpr std::string_view anchor_line_syntax = "^L[0-9]+(-L?[0-9]+)?$";

// The subjects' types, the last two free of the line suffix rule.
inline constexpr std::array<std::string_view, 8> subject_types = {"entity", "activity", "repo",  "item",
                                                                  "clause", "module",   "entry", "scope"};

// Whitespace: these 29 code points, as ranges.
struct Range {
    char32_t first;
    char32_t last;
};
static_assert(sizeof(Range) == 8, "Range's size is pinned: two code points");
inline constexpr std::array<Range, 10> WHITESPACE = {
    Range{0x09, 0x0D},     Range{0x1C, 0x20},     Range{0x85, 0x85},     Range{0xA0, 0xA0},
    Range{0x1680, 0x1680}, Range{0x2000, 0x200A}, Range{0x2028, 0x2029}, Range{0x202F, 0x202F},
    Range{0x205F, 0x205F}, Range{0x3000, 0x3000},
};

constexpr bool is_space(char32_t p) noexcept {
    for (const Range& r : WHITESPACE) {
        if (p >= r.first && p <= r.last) {
            return true;
        }
    }
    return false;
}
constexpr bool is_control(char32_t p) noexcept { return p <= 0x1F || (p >= 0x7F && p <= 0x9F); }

namespace detail {

constexpr bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

// The start of the run of digits ending before end.
constexpr std::size_t digits_from(std::string_view s, std::size_t end) noexcept {
    std::size_t at = end;
    while (at > 0 && digit(s[at - 1])) {
        --at;
    }
    return at;
}

// Whether s[at - 1] is c.
constexpr bool before(std::string_view s, std::size_t at, char c) noexcept { return at > 0 && s[at - 1] == c; }

// What a text's code points are: how many, and whether any is a control
// character, whitespace, U+2028 or U+2029, or a fault in UTF-8.
struct Scan {
    std::size_t length = 0;
    bool control = false;
    bool space = false;
    bool separator = false;
    bool fault = false;
};
static_assert(sizeof(Scan) == 16, "Scan's size is pinned: a count and four flags");

constexpr Scan scan(std::string_view text) noexcept {
    Scan out;
    std::size_t at = 0;
    while (at < text.size()) {
        const utf::Read r = utf::next(text, at);
        if (r.size == 0) {
            out.fault = true;
            return out;
        }
        out.control = out.control || is_control(r.point);
        out.space = out.space || is_space(r.point);
        out.separator = out.separator || r.point == 0x2028 || r.point == 0x2029;
        ++out.length;
        at += r.size;
    }
    return out;
}

// Whether text matches pattern, compiled over allocator: two allocations,
// freed before it returns.
template <typename A>
constexpr bool matches(std::string_view pattern, std::string_view text, const A& allocator) {
    const matcher::Basic<A> automaton = matcher::compile(pattern, allocator);
    return matcher::hit(automaton, text);
}

}  // namespace detail

// Whether text ends in a line suffix: :N, :N:M, :N-M, #LN, #LN-M or #LN-LM.
constexpr bool line_suffix(std::string_view s) noexcept {
    using detail::before;
    const std::size_t d1 = detail::digits_from(s, s.size());
    if (d1 == s.size()) {
        return false;
    }
    if (before(s, d1, ':') || (before(s, d1, 'L') && before(s, d1 - 1, '#'))) {
        return true;  // :N, the end of :N:M, #LN
    }
    // :N-M, #LN-M, then #LN-LM.
    const std::size_t dash = before(s, d1, '-') ? d1 - 1 : (before(s, d1, 'L') && before(s, d1 - 1, '-') ? d1 - 2 : 0);
    if (dash == 0) {
        return false;
    }
    const std::size_t d2 = detail::digits_from(s, dash);
    if (d2 == dash) {
        return false;
    }
    return (dash == d1 - 1 && before(s, d2, ':')) || (before(s, d2, 'L') && before(s, d2 - 1, '#'));
}

// The one path rule.
constexpr bool check_path(std::string_view path) noexcept {
    const detail::Scan s = detail::scan(path);
    if (s.fault || s.control || s.separator || s.length < 1 || s.length > path_limit || line_suffix(path) ||
        path.find('\\') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t end = path.find('/', start);
        const std::string_view segment = path.substr(start, end == std::string_view::npos ? end : end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;  // a leading /, a // or a trailing / among them
        }
        if (end == std::string_view::npos) {
            return true;
        }
        start = end + 1;
    }
}

// A qualified name.
constexpr bool check_name(std::string_view name) noexcept {
    const detail::Scan s = detail::scan(name);
    return !s.fault && !s.control && !s.space && s.length >= 1 && s.length <= name_limit && !line_suffix(name);
}

// A subject; two allocations for a repo subject's repository syntax.
template <typename A = std::allocator<char>>
constexpr bool check_subject(std::string_view subject, const A& allocator = A()) {
    const detail::Scan s = detail::scan(subject);
    const std::size_t colon = subject.find(':');
    if (s.fault || s.control || s.space || s.length > subject_limit || colon == std::string_view::npos ||
        colon + 1 == subject.size() || !header::among(subject_types, subject.substr(0, colon))) {
        return false;
    }
    const std::string_view type = subject.substr(0, colon);
    if (type != "entry" && type != "scope" && line_suffix(subject)) {
        return false;
    }
    if (type != "repo") {
        return true;
    }
    const std::string_view rest = subject.substr(colon + 1);
    const std::size_t slash = rest.find('/');
    const std::size_t hash = rest.find('#');
    if (hash < slash) {
        return false;  // a # before any path
    }
    if (!detail::matches(repository_syntax, rest.substr(0, slash), allocator)) {
        return false;
    }
    if (slash == std::string_view::npos) {
        return true;
    }
    const std::string_view after = rest.substr(slash + 1);
    const std::size_t mark = after.find('#');
    return check_path(after.substr(0, mark)) && (mark == std::string_view::npos || check_name(after.substr(mark + 1)));
}

namespace detail {

constexpr std::optional<Field> field_of(std::string_view key) noexcept {
    for (std::size_t i = 0; i < field_count; ++i) {
        if (field_names[i] == key) {
            return static_cast<Field>(i);
        }
    }
    return std::nullopt;
}

constexpr std::optional<Type> type_of(std::string_view text) noexcept {
    for (std::size_t i = 0; i < type_names.size(); ++i) {
        if (type_names[i] == text) {
            return static_cast<Type>(i);
        }
    }
    return std::nullopt;
}

// A present field's value, a string not empty, against its syntax.
template <typename A>
constexpr bool value_syntax(Field f, std::string_view text, const A& allocator) {
    switch (f) {
        case Field::id: return matches(header::id_syntax, text, allocator);
        case Field::repository: return matches(repository_syntax, text, allocator);
        case Field::path: return check_path(text);
        case Field::revision: return matches(revision_syntax, text, allocator);
        case Field::name: return check_name(text);
        case Field::anchor:
            return scan(text).length <= anchor_limit && !matches(anchor_line_syntax, text, allocator);
        case Field::digest: return matches(header::content_syntax, text, allocator);
        case Field::client: return matches(client_syntax, text, allocator);
        case Field::session: return matches(session_syntax, text, allocator);
        case Field::message: return matches(message_syntax, text, allocator);
        case Field::type:
        case Field::command:
        case Field::none: return true;
    }
    return true;
}

}  // namespace detail

// The reference at node i of v: its first fault, or none.
template <typename B>
constexpr std::optional<RefError> try_check_reference(const canon::Basic<B>& v, json::Index i = 0) {
    if (v.empty() || v.kind(i) != json::Kind::object) {
        return RefError{Code::type, Field::none, i};
    }
    const json::Index end = v.next(i);
    // The type first, which decides the keys; its fault waits on theirs.
    const std::optional<json::Index> t = v.find(i, "type");
    const bool text = t && v.kind(*t) == json::Kind::string;
    const std::optional<Type> type = text ? detail::type_of(v.text(*t)) : std::nullopt;
    const Fields holds = type ? SHAPES[static_cast<std::size_t>(*type)].holds : any_type;
    for (json::Index k = i + 1; k < end; k = v.next(k + 1)) {
        const std::optional<Field> f = detail::field_of(v.text(k));
        if (!f || (holds & bit(*f)) == 0) {
            return RefError{Code::unknown_field, f.value_or(Field::none), k};
        }
    }
    if (!type) {
        const Code code = !t ? Code::missing : !text ? Code::type : v.text(*t).empty() ? Code::empty : Code::syntax;
        return RefError{code, Field::type, t ? *t - 1 : i};
    }
    const Fields required = SHAPES[static_cast<std::size_t>(*type)].required;
    for (std::size_t f = 0; f < field_count; ++f) {
        if ((required & bit(static_cast<Field>(f))) != 0 && !v.find(i, field_names[f])) {
            return RefError{Code::missing, static_cast<Field>(f), i};
        }
    }
    // Each member is a field of this type now; type, empty, too_long and
    // syntax in turn, each over the members in order.
    for (const Code code : {Code::type, Code::empty, Code::too_long, Code::syntax}) {
        for (json::Index k = i + 1; k < end; k = v.next(k + 1)) {
            const Field f = detail::field_of(v.text(k)).value_or(Field::none);
            const bool fault = code == Code::type      ? v.kind(k + 1) != json::Kind::string
                               : code == Code::empty    ? v.text(k + 1).empty()
                               : code == Code::too_long ? f == Field::command && utf::length(v.text(k + 1)) > command_limit
                                                        : !detail::value_syntax(f, v.text(k + 1), v.allocator());
            if (fault) {
                return RefError{code, f, k};
            }
        }
    }
    return std::nullopt;
}

// try_check_reference, a fault thrown as RefError.
template <typename B>
constexpr void check_reference(const canon::Basic<B>& v, json::Index i = 0) {
    if (const std::optional<RefError> fault = try_check_reference(v, i)) {
        throw *fault;
    }
}

}  // namespace qiven3::entry::refs
