#pragma once

// projectmap_files: the map files' parser, the registry's and the marks
// file's, and the marks file's bytes. Each reads through canon's strict
// parser, its key rule each key's place, since repository names hold
// hyphens: the registry's repository names, then path and ref under each;
// the marks file's at, marks and read, then version ids in marks and
// repository names in read; any other place the key syntax. A key off its
// place is key_syntax. The registry takes any layout (CRLF, indented, a
// final line end) and skips the comparison; the marks file is one
// canonical line and its LF, the parser reading the bytes before it. Then
// the shape: a key the place does not hold unknown_field, one absent
// missing, a value of the wrong JSON type type, a value off its syntax
// syntax, the first in that order deciding. A refused file is FileError:
// parse with canon's code, or the shape's code.
//
// The registry: an object from each repository name
// (^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$) to an object of exactly path, an
// absolute path (Python's os.path.isabs on the system built for) with no
// control character (U+0000 to U+001F, U+007F to U+009F), and ref,
// ^[A-Za-z0-9][A-Za-z0-9._/-]{0,199}$. Registry holds the parsed value and
// reads it by index. The marks file: at, a time; marks, from each marked
// version's id to fresh or stale; read, from each repository read to its
// commit's id, 40 lowercase hex. Marks holds its rows by value, each list
// sorted by its key; find gives a version's MapMark with the file's at, and
// none for a version the file does not name. marks_bytes writes the file's
// bytes from Marks: MapError at for a Marks with no at.
//
// Readings: a marks file with no final LF reads not_canonical, since its
// bytes are not its canonical line and LF; os.path.isabs is ntpath's on
// Windows (a code point, then : and a separator, or two separators at the
// start, / and \ alike) and posixpath's elsewhere (a leading /).
//
// Allocations: parse_registry canon's three, all held by the Registry;
// parse_marks canon's four, freed before it returns, and one for each
// non-empty list, its rows reserved exactly and held by the Marks;
// Marks::put and put_read the list's growth alone, none once reserve has
// made room; marks_bytes one, the text's, its size summed with checks (a
// sum past std::size_t is MemoryError budget), into the caller's string,
// replaced only on success; find, commit and the accessors none. Each is
// constexpr and a template on the allocator, so its vectors run in
// constant evaluation too; it reads by index, with no pointer arithmetic.
//
// thread: none

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/projectmap.hpp"
#include "entry/served.hpp"
#include "entry/validator.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::projectmap {

// The shape's codes, after canon's: parse holds canon's.
enum class FileCode : std::uint8_t { parse, unknown_field, missing, type, syntax };
inline constexpr std::array<std::string_view, 5> file_code_names = {"parse", "unknown_field", "missing", "type",
                                                                     "syntax"};
constexpr std::string_view name(FileCode code) noexcept {
    return file_code_names[static_cast<std::size_t>(code)];
}

struct FileError {
    FileCode code = FileCode::parse;
    ParseCode parse = ParseCode::syntax;
    constexpr bool operator==(const FileError&) const = default;
};
static_assert(sizeof(FileError) == 2, "FileError's size is pinned: its code and canon's");

// The system whose os.path.isabs a registry's path meets.
enum class System : std::uint8_t { windows, posix };
#if defined(_WIN32)
inline constexpr System NATIVE = System::windows;
#else
inline constexpr System NATIVE = System::posix;
#endif

namespace detail {

constexpr bool alnum(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
constexpr bool lower_hex(std::string_view s) noexcept {
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
// ^[A-Za-z0-9][A-Za-z0-9<more>]{0,max-1}$.
constexpr bool name_like(std::string_view s, std::size_t max, std::string_view more) noexcept {
    if (s.empty() || s.size() > max || !alnum(s.front())) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [&](char c) { return alnum(c) || more.find(c) != std::string_view::npos; });
}

// The parser's key: only it makes one, so only a checked value reaches a
// Registry.
struct Parser;
class Checked {
    friend struct Parser;
    constexpr Checked() = default;
};

}  // namespace detail

constexpr bool repository(std::string_view s) noexcept { return detail::name_like(s, 100, "._-"); }
constexpr bool ref(std::string_view s) noexcept { return detail::name_like(s, 200, "._/-"); }
constexpr bool version_id(std::string_view s) noexcept {
    return s.size() == 71 && s.starts_with("sha256:") && detail::lower_hex(s.substr(7));
}
constexpr bool commit_id(std::string_view s) noexcept { return s.size() == 40 && detail::lower_hex(s); }

// os.path.isabs on system, over UTF-8 text.
constexpr bool absolute(std::string_view path, System system) noexcept {
    const auto sep = [&](std::size_t i) { return i < path.size() && (path[i] == '/' || path[i] == '\\'); };
    if (system == System::posix) {
        return path.starts_with('/');
    }
    if (sep(0) && sep(1)) {
        return true;
    }
    if (path.empty()) {
        return false;
    }
    const auto lead = static_cast<unsigned char>(path.front());
    const std::size_t first = lead < 0x80U ? 1 : lead < 0xE0U ? 2 : lead < 0xF0U ? 3 : 4;
    return first < path.size() && path[first] == ':' && sep(first + 1);
}

// Whether text holds a control character, U+0000 to U+001F or U+007F to
// U+009F, over valid UTF-8.
constexpr bool control(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x20U || c == 0x7FU ||
            (c == 0xC2U && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) <= 0x9FU)) {
            return true;
        }
    }
    return false;
}

// The key rules, by place.
struct RegistryKeys {
    constexpr bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        return place.level == 1 ? repository(key) : canon::key_syntax(key);
    }
};
struct MarksKeys {
    constexpr bool operator()(std::string_view key, const canon::PlaceView& place) const noexcept {
        if (place.level == 2 && place.keys.front() == "marks") {
            return version_id(key);
        }
        if (place.level == 2 && place.keys.front() == "read") {
            return repository(key);
        }
        return canon::key_syntax(key);
    }
};

// A marked version: its id and fresh or stale.
struct MarkRow {
    validator::Id id;
    served::Mark mark = served::Mark::stale;
};
static_assert(sizeof(MarkRow) == 73, "MarkRow's size is pinned: an Id and its mark");

// A repository read: its name, at most 100 characters, and its commit's id.
struct ReadRow {
    std::array<char, 100> name{};
    std::uint8_t size = 0;
    std::array<char, 40> commit{};
    [[nodiscard]] constexpr std::string_view repository() const noexcept { return {name.data(), size}; }
    [[nodiscard]] constexpr std::string_view commit_id() const noexcept { return {commit.data(), commit.size()}; }
};
static_assert(sizeof(ReadRow) == 141, "ReadRow's size is pinned: a name, its size and a commit id");

// The registry, read: its repositories in the file's order.
template <typename A = std::allocator<char>>
class Registry {
public:
    constexpr explicit Registry(const A& allocator = A()) noexcept : value_(allocator) {}

    [[nodiscard]] constexpr A value_allocator() const { return value_.allocator(); }
    [[nodiscard]] constexpr std::size_t size() const { return value_.empty() ? 0 : value_.count(0); }
    [[nodiscard]] constexpr std::string_view name(std::size_t i) const { return value_.text(key(i)); }
    [[nodiscard]] constexpr std::string_view path(std::size_t i) const { return member(i, "path"); }
    [[nodiscard]] constexpr std::string_view ref(std::size_t i) const { return member(i, "ref"); }
    // A repository's place, or none for a name the registry lacks.
    [[nodiscard]] constexpr std::optional<std::size_t> find(std::string_view repository) const {
        for (std::size_t i = 0; i < size(); ++i) {
            if (name(i) == repository) {
                return i;
            }
        }
        return std::nullopt;
    }

    // The parser's one way in: a value whose shape it has checked.
    constexpr void take(canon::Basic<A>&& value, const detail::Checked& /*checked*/) { value_ = std::move(value); }

private:
    [[nodiscard]] constexpr json::Index key(std::size_t i) const {
        if (i >= size()) {
            memory::fail_fast("projectmap: a registry index past its size");
        }
        json::Index k = 1;
        for (std::size_t n = 0; n < i; ++n) {
            k = value_.next(k + 1);
        }
        return k;
    }
    [[nodiscard]] constexpr std::string_view member(std::size_t i, std::string_view field) const {
        const std::optional<json::Index> at = value_.find(key(i) + 1, field);
        if (!at) {
            memory::fail_fast("projectmap: a registry member without its field");
        }
        return value_.text(*at);
    }

    canon::Basic<A> value_;
};
static_assert(sizeof(Registry<>) == sizeof(canon::Basic<>), "Registry's size is pinned: its value");

// The marks file, read or built: its at, its marks and its reads.
template <typename A = std::allocator<char>>
class Marks {
    using MarkAlloc = typename std::allocator_traits<A>::template rebind_alloc<MarkRow>;
    using ReadAlloc = typename std::allocator_traits<A>::template rebind_alloc<ReadRow>;

public:
    constexpr explicit Marks(const A& allocator = A()) noexcept
        : marks_(MarkAlloc(allocator)), read_(ReadAlloc(allocator)) {}

    [[nodiscard]] constexpr A allocator() const { return A(marks_.get_allocator()); }

    // The run's start: false, at unchanged, off a time's syntax.
    constexpr bool set_at(std::string_view at) {
        if (utc::check_at(at).has_value()) {
            return false;
        }
        std::copy(at.begin(), at.end(), at_.begin());
        has_at_ = true;
        return true;
    }
    [[nodiscard]] constexpr std::optional<std::string_view> at() const noexcept {
        return has_at_ ? std::optional<std::string_view>(std::string_view(at_.data(), at_.size())) : std::nullopt;
    }
    constexpr void reserve(std::size_t marks, std::size_t reads) {
        marks_.reserve(marks);
        read_.reserve(reads);
    }
    // A version's mark: false for an id off its syntax, a mark but fresh
    // or stale, or an id already marked.
    constexpr bool put(std::string_view id, served::Mark mark) {
        if (!version_id(id) || (mark != served::Mark::fresh && mark != served::Mark::stale)) {
            return false;
        }
        const auto at = std::lower_bound(marks_.begin(), marks_.end(), id,
                                         [](const MarkRow& r, std::string_view k) { return r.id.view() < k; });
        if (at != marks_.end() && at->id.view() == id) {
            return false;
        }
        marks_.insert(at, MarkRow{validator::Id::of(id), mark});
        return true;
    }
    // A repository read and its commit: false off their syntaxes, or a
    // repository already read.
    constexpr bool put_read(std::string_view name, std::string_view commit) {
        if (!repository(name) || !commit_id(commit)) {
            return false;
        }
        const auto at = std::lower_bound(read_.begin(), read_.end(), name,
                                         [](const ReadRow& r, std::string_view k) { return r.repository() < k; });
        if (at != read_.end() && at->repository() == name) {
            return false;
        }
        ReadRow row;
        std::copy(name.begin(), name.end(), row.name.begin());
        row.size = static_cast<std::uint8_t>(name.size());
        std::copy(commit.begin(), commit.end(), row.commit.begin());
        read_.insert(at, row);
        return true;
    }
    // A version's mark with the file's at; none for a version not named.
    [[nodiscard]] constexpr std::optional<served::MapMark> find(std::string_view id) const {
        const auto at = std::lower_bound(marks_.begin(), marks_.end(), id,
                                         [](const MarkRow& r, std::string_view k) { return r.id.view() < k; });
        if (at == marks_.end() || at->id.view() != id || !has_at_) {
            return std::nullopt;
        }
        return served::map_mark(at->mark, std::string_view(at_.data(), at_.size()));
    }
    // A repository's commit, or none.
    [[nodiscard]] constexpr std::optional<std::string_view> commit(std::string_view name) const {
        for (const ReadRow& r : read_) {
            if (r.repository() == name) {
                return r.commit_id();
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return marks_.size(); }
    [[nodiscard]] constexpr std::size_t read_size() const noexcept { return read_.size(); }
    [[nodiscard]] constexpr const MarkRow& row(std::size_t i) const { return marks_.at(i); }
    [[nodiscard]] constexpr const ReadRow& read_row(std::size_t i) const { return read_.at(i); }

private:
    std::array<char, utc::at_size> at_{};
    bool has_at_ = false;
    std::vector<MarkRow, MarkAlloc> marks_;
    std::vector<ReadRow, ReadAlloc> read_;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Marks<>) == 32 + (2 * sizeof(std::vector<MarkRow>)),
              "Marks' size is pinned: its at, the flag and two lists");
#endif

namespace detail {

// The first code of the shape's faults: the lowest seen.
struct Worst {
    std::optional<FileCode> code;
    constexpr void note(FileCode c) noexcept {
        if (!code || c < *code) {
            code = c;
        }
    }
};

// A string member's fault: type off a string, syntax off ok.
template <typename A, typename F>
constexpr void string_value(const json::Basic<A>& v, json::Index at, Worst& worst, const F& ok) {
    if (v.kind(at) != json::Kind::string) {
        worst.note(FileCode::type);
    } else if (!ok(v.text(at))) {
        worst.note(FileCode::syntax);
    }
}

// Each member of object at, by keys: for a key of names, f(index of the
// name, value); another key unknown_field; a name absent missing; at not an
// object type.
template <typename A, std::size_t N, typename F>
constexpr void members(const json::Basic<A>& v, json::Index at, const std::array<std::string_view, N>& names,
                       Worst& worst, const F& f) {
    if (v.kind(at) != json::Kind::object) {
        worst.note(FileCode::type);
        return;
    }
    std::array<bool, N> seen{};
    for (json::Index k = at + 1; k < v.next(at); k = v.next(k + 1)) {
        const auto n = std::find(names.begin(), names.end(), v.text(k));
        if (n == names.end()) {
            worst.note(FileCode::unknown_field);
            continue;
        }
        const auto i = static_cast<std::size_t>(n - names.begin());
        seen.at(i) = true;
        f(i, k + 1);
    }
    if (std::find(seen.begin(), seen.end(), false) != seen.end()) {
        worst.note(FileCode::missing);
    }
}

inline constexpr std::array<std::string_view, 2> REGISTRY_FIELDS = {"path", "ref"};
inline constexpr std::array<std::string_view, 3> MARKS_FIELDS = {"at", "marks", "read"};

// Each member's value of object at, its key free, through f; at not an
// object type.
template <typename A, typename F>
constexpr void each(const json::Basic<A>& v, json::Index at, Worst& worst, const F& f) {
    if (v.kind(at) != json::Kind::object) {
        worst.note(FileCode::type);
        return;
    }
    for (json::Index k = at + 1; k < v.next(at); k = v.next(k + 1)) {
        f(k + 1);
    }
}

struct Parser {
    template <typename A>
    static constexpr std::optional<FileError> registry(std::string_view data, Registry<A>& out, System system) {
        canon::Basic<A> v(out.value_allocator());
        if (const std::optional<ParseCode> p = canon::try_parse(data, v, RegistryKeys{}, canon::Layout::any)) {
            return FileError{FileCode::parse, *p};
        }
        Worst worst;
        each(v, 0, worst, [&](json::Index e) {
            members(v, e, REGISTRY_FIELDS, worst, [&](std::size_t i, json::Index m) {
                if (i == 0) {
                    string_value(v, m, worst, [&](std::string_view p) { return absolute(p, system) && !control(p); });
                } else {
                    string_value(v, m, worst, [](std::string_view r) { return ref(r); });
                }
            });
        });
        if (worst.code) {
            return FileError{*worst.code, ParseCode::syntax};
        }
        out.take(std::move(v), Checked{});
        return std::nullopt;
    }

    template <typename A>
    static constexpr std::optional<FileError> marks(std::string_view data, Marks<A>& out) {
        if (!data.ends_with('\n')) {
            return FileError{FileCode::parse, ParseCode::not_canonical};
        }
        canon::Basic<A> v(out.allocator());
        if (const std::optional<ParseCode> p = canon::try_parse(data.substr(0, data.size() - 1), v, MarksKeys{})) {
            return FileError{FileCode::parse, *p};
        }
        Worst worst;
        std::array<json::Index, 3> place{};
        members(v, 0, MARKS_FIELDS, worst, [&](std::size_t i, json::Index m) {
            place.at(i) = m;
            if (i == 0) {
                string_value(v, m, worst, [](std::string_view t) { return !utc::check_at(t).has_value(); });
            } else if (i == 1) {
                each(v, m, worst, [&](json::Index x) {
                    string_value(v, x, worst, [](std::string_view t) { return t == "fresh" || t == "stale"; });
                });
            } else {
                each(v, m, worst, [&](json::Index x) { string_value(v, x, worst, commit_id); });
            }
        });
        if (worst.code) {
            return FileError{*worst.code, ParseCode::syntax};
        }
        Marks<A> m(out.allocator());
        m.reserve(v.count(place[1]), v.count(place[2]));
        static_cast<void>(m.set_at(v.text(place[0])));
        for (json::Index k = place[1] + 1; k < v.next(place[1]); k = v.next(k + 1)) {
            static_cast<void>(m.put(v.text(k), v.text(k + 1) == "fresh" ? served::Mark::fresh : served::Mark::stale));
        }
        for (json::Index k = place[2] + 1; k < v.next(place[2]); k = v.next(k + 1)) {
            static_cast<void>(m.put_read(v.text(k), v.text(k + 1)));
        }
        out = std::move(m);
        return std::nullopt;
    }
};

}  // namespace detail

// The registry's bytes read into out, replaced only on success; the
// first fault, or none.
template <typename A>
constexpr std::optional<FileError> try_parse_registry(std::string_view data, Registry<A>& out,
                                                      System system = NATIVE) {
    return detail::Parser::registry(data, out, system);
}

// try_parse_registry into a new Registry, a fault thrown as FileError.
template <typename A = std::allocator<char>>
constexpr Registry<A> parse_registry(std::string_view data, const A& allocator = A(), System system = NATIVE) {
    Registry<A> out(allocator);
    if (const std::optional<FileError> fault = try_parse_registry(data, out, system)) {
        throw *fault;
    }
    return out;
}

// The marks file's bytes read into out, replaced only on success; the
// first fault, or none. A caller reads a refused file as no file.
template <typename A>
constexpr std::optional<FileError> try_parse_marks(std::string_view data, Marks<A>& out) {
    return detail::Parser::marks(data, out);
}

// try_parse_marks into a new Marks, a fault thrown as FileError.
template <typename A = std::allocator<char>>
constexpr Marks<A> parse_marks(std::string_view data, const A& allocator = A()) {
    Marks<A> out(allocator);
    if (const std::optional<FileError> fault = try_parse_marks(data, out)) {
        throw *fault;
    }
    return out;
}

// The marks file's bytes, its canonical line and LF, into out, replaced
// only on success: MapError at with no at.
template <typename A, typename S>
constexpr void marks_bytes(const Marks<A>& marks, S& out) {
    const std::optional<std::string_view> at = marks.at();
    if (!at) {
        throw MapError{Code::at};
    }
    // {"at":"<at>","marks":{ ... },"read":{ ... }} and LF: each mark
    // "<id>":"fresh" 81, each read "<name>":"<commit>" 45 and its name,
    // a comma before each but the first of its list.
    std::optional<std::size_t> size = memory::mul(marks.size(), 82);
    for (std::size_t i = 0; i < marks.read_size() && size; ++i) {
        size = memory::add(*size, std::size_t{46} + marks.read_row(i).size);
    }
    std::size_t firsts = 0;
    for (const std::size_t n : {marks.size(), marks.read_size()}) {
        firsts += n > 0 ? 1 : 0;
    }
    size = size ? memory::add(*size, 7 + utc::at_size + 11 + 10 + 2 + 1 - firsts) : std::nullopt;
    if (!size) {
        throw memory::MemoryError::budget("projectmap", SIZE_MAX, SIZE_MAX, 0);
    }
    S text(out.get_allocator());
    text.reserve(*size);
    text.append("{\"at\":\"");
    text.append(*at);
    text.append("\",\"marks\":{");
    for (std::size_t i = 0; i < marks.size(); ++i) {
        text.append(i == 0 ? "\"" : ",\"");
        text.append(marks.row(i).id.view());
        text.append(marks.row(i).mark == served::Mark::fresh ? "\":\"fresh\"" : "\":\"stale\"");
    }
    text.append("},\"read\":{");
    for (std::size_t i = 0; i < marks.read_size(); ++i) {
        text.append(i == 0 ? "\"" : ",\"");
        text.append(marks.read_row(i).repository());
        text.append("\":\"");
        text.append(marks.read_row(i).commit_id());
        text.append("\"");
    }
    text.append("}}\n");
    out = std::move(text);
}

}  // namespace qiven3::entry::projectmap
