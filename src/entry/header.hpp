#pragma once

// header: an entry's header, its 11 fields in their fixed order and the
// stored line's id. parse_line reads a stored header line through
// canon::parse, the strict parser, and keeps what it finds: each key's
// value as text, an integer or another type, and whether a key lies outside
// the set; it refuses only what is no header line at all (the parser's
// ParseError, or a root that is no object, HeaderError syntax). check then
// reads the header in one fixed order, the first fault deciding:
// unknown_key, missing_key, empty, owner, syntax, reserved, lane,
// reflect_kind, seq, target, content, id_mismatch. Its syntaxes are
// matcher's patterns, each compiled within the call; at is utc's; tag,
// kind and basis are closed sets. make builds a header from the caller's
// fields, its id computed unless given; line writes the stored line, the
// present fields and id as canonical JSON. stamp gives a new entry's at:
// the clock, raised to the parent's at if earlier, and to 1 ms past the
// target's if not later.
//
// Allocations: parse_line canon's four and one, the header's text; make
// one; check two for each syntax it compiles (scope, lane, writer, the id's
// for id, parent and target, and content when present: eight or ten), each
// freed before it returns; line four, the value's two and canon's two; the
// accessors and stamp none. A size past json::max_input is MemoryError
// budget. Each but stamp's clock call is constexpr and a template on the
// allocator or the string type, so its vectors run in constant evaluation
// too; it reads by index, with no pointer arithmetic. header logs nothing.
//
// thread: none

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "entry/matcher.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "qiven3/utc.hpp"

namespace qiven3::entry::header {

// The codes, in the order check reads them.
enum class Code : std::uint8_t {
    unknown_key,
    missing_key,
    empty,
    owner,
    syntax,
    reserved,
    lane,
    reflect_kind,
    seq,
    target,
    content,
    id_mismatch,
};
inline constexpr std::array<std::string_view, 12> code_names = {
    "unknown_key", "missing_key", "empty",        "owner",  "syntax",  "reserved",
    "lane",        "reflect_kind", "seq",         "target", "content", "id_mismatch",
};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }

struct HeaderError {
    Code code;
};

// The 11 fields in their fixed order, the framing's, then the line's id.
enum class Slot : std::uint8_t { tag, scope, lane, seq, parent, kind, writer, basis, at, target, content, id };
inline constexpr std::size_t field_count = 11;
inline constexpr std::size_t slot_count = 12;
inline constexpr std::array<std::string_view, slot_count> slot_names = {
    "tag", "scope", "lane", "seq", "parent", "kind", "writer", "basis", "at", "target", "content", "id",
};
// The slots in the line's key order.
inline constexpr std::array<Slot, slot_count> line_order = {
    Slot::at,     Slot::basis, Slot::content, Slot::id,  Slot::kind,   Slot::lane,
    Slot::parent, Slot::scope, Slot::seq,     Slot::tag, Slot::target, Slot::writer,
};
// Whether a slot may be absent: parent, target and content alone.
constexpr bool optional_slot(Slot s) noexcept { return s == Slot::parent || s == Slot::target || s == Slot::content; }

// The closed values: the tag, the kinds of this minor version, the change
// kinds (which require a target), the reflection writer's kinds, the bases.
inline constexpr std::string_view tag_value = "qiven-entry/1";
inline constexpr std::array<std::string_view, 22> kind_names = {
    "direction", "account", "entity", "observation", "claim",      "decision", "question", "lesson",
    "episode",   "relation", "goal",  "clause",      "module",     "item",     "checkpoint", "measure",
    "map",       "check",   "accept", "retire",      "forget",     "usage",
};
inline constexpr std::array<std::string_view, 4> change_kinds = {"check", "accept", "retire", "forget"};
inline constexpr std::array<std::string_view, 2> reflect_kinds = {"claim", "lesson"};
inline constexpr std::array<std::string_view, 4> basis_names = {"owner-words", "observed", "claimed", "derived"};
inline constexpr std::string_view reflect_role = "reflect";
inline constexpr std::string_view owner_role = "owner";

// The syntaxes, each matching the whole value.
inline constexpr std::string_view scope_syntax = "^(person|life|project:[a-z0-9][a-z0-9-]{0,47})$";
inline constexpr std::string_view lane_syntax = "^[a-z][a-z0-9-]{0,31}$";
inline constexpr std::string_view writer_syntax = "^(loop|import|[a-z][a-z0-9-]{0,31}/s[1-9][0-9]{0,8})$";
inline constexpr std::string_view id_syntax = "^sha256:[0-9a-f]{64}$";
inline constexpr std::string_view content_syntax = "^hmac-sha256:[0-9a-f]{64}$";
inline constexpr std::int64_t seq_max = json::max_integer;

// What a slot holds as read: nothing, text, an integer, or another type.
enum class Form : std::uint8_t { absent, text, integer, other };

// A header's fields as a caller gives them, viewing the caller's bytes
// within the call to make; an id left absent is computed.
struct FieldsView {
    std::string_view tag = tag_value;
    std::string_view scope;
    std::string_view lane;
    std::int64_t seq = 1;
    std::optional<std::string_view> parent;
    std::string_view kind;
    std::string_view writer;
    std::string_view basis;
    std::string_view at;
    std::optional<std::string_view> target;
    std::optional<std::string_view> content;
    std::optional<std::string_view> id;
};

constexpr bool among(std::span<const std::string_view> set, std::string_view value) noexcept {
    for (const std::string_view s : set) {
        if (s == value) {
            return true;
        }
    }
    return false;
}

// A writer's role: up to its '/', or the whole writer (loop, import).
constexpr std::string_view role(std::string_view writer) noexcept { return writer.substr(0, writer.find('/')); }

namespace detail {

constexpr std::size_t index(Slot s) noexcept { return static_cast<std::size_t>(s); }

// The parts a header is built from, viewing the caller's bytes within the
// constructor's call.
struct PartsView {
    std::array<Form, slot_count> forms{};
    std::array<std::string_view, slot_count> bytes = memory::each_value<std::string_view, slot_count>();
    std::int64_t seq = 0;
    bool unknown = false;
};

// An integer's decimal digits, written into digits.
constexpr std::string_view decimal(std::int64_t value, std::array<char, 20>& digits) noexcept {
    std::uint64_t magnitude = value < 0 ? 0U - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
    std::size_t at = digits.size();
    do {
        digits.at(--at) = static_cast<char>('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U);
    if (value < 0) {
        digits.at(--at) = '-';
    }
    return std::string_view(digits.data(), digits.size()).substr(at);
}

constexpr std::optional<std::size_t> slot_of(std::string_view key) noexcept {
    for (std::size_t i = 0; i < slot_count; ++i) {
        if (slot_names[i] == key) {
            return i;
        }
    }
    return std::nullopt;
}

[[noreturn]] inline void too_large(std::size_t request) {
    throw memory::MemoryError::budget("header", json::max_input, request, 0);
}

// An id's text, held in place: digest's hex writer writes into it with no
// allocation.
class IdText {
public:
    struct None {};
    constexpr IdText() noexcept = default;
    constexpr explicit IdText(None /*none*/) noexcept {}
    [[nodiscard]] constexpr None get_allocator() const noexcept { return {}; }
    constexpr void reserve(std::size_t n) const {
        if (n > bytes_.size()) {
            memory::fail_fast("header: an id past 71 bytes");
        }
    }
    constexpr void append(std::string_view s) {
        for (const char c : s) {
            bytes_.at(size_++) = c;
        }
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(bytes_.data(), bytes_.size()).substr(0, size_);
    }

private:
    std::array<char, 71> bytes_{};
    std::size_t size_ = 0;
};

}  // namespace detail

// A header: its slots' bytes in one text, each slot's place and form, seq's
// integer, and whether a key lay outside the set. Move-only.
template <typename A = std::allocator<char>>
class Header {
public:
    using allocator_type = A;
    using Text = std::basic_string<char, std::char_traits<char>, A>;

    constexpr explicit Header(const A& allocator = A()) noexcept : text_(allocator) {}
    // The builder's: one allocation, the parts' bytes copied.
    constexpr Header(const detail::PartsView& parts, const A& allocator) : text_(allocator) {
        std::size_t total = 0;
        for (const std::string_view b : parts.bytes) {
            const std::optional<std::size_t> next = memory::add(total, b.size());
            if (!next || *next > json::max_input) {
                detail::too_large(next.value_or(total));
            }
            total = *next;
        }
        text_.reserve(total);
        for (std::size_t i = 0; i < slot_count; ++i) {
            places_.at(i) = Place{static_cast<std::uint32_t>(text_.size()),
                                  static_cast<std::uint32_t>(parts.bytes.at(i).size()), parts.forms.at(i)};
            text_.append(parts.bytes.at(i));
        }
        seq_ = parts.seq;
        unknown_ = parts.unknown;
    }
    Header(const Header&) = delete;
    Header& operator=(const Header&) = delete;
    constexpr Header(Header&&) noexcept = default;
    constexpr Header& operator=(Header&&) noexcept = default;
    constexpr ~Header() = default;

    [[nodiscard]] constexpr A get_allocator() const { return text_.get_allocator(); }
    [[nodiscard]] constexpr Form form(Slot s) const noexcept { return places_[detail::index(s)].form; }
    [[nodiscard]] constexpr bool present(Slot s) const noexcept { return form(s) != Form::absent; }
    // A slot's text, or an integer's decimal digits; none when absent or
    // of another type.
    [[nodiscard]] constexpr std::optional<std::string_view> text(Slot s) const {
        const Place& p = places_.at(detail::index(s));
        if (p.form != Form::text && p.form != Form::integer) {
            return std::nullopt;
        }
        return std::string_view(text_).substr(p.at, p.size);
    }
    // A slot's text as text(s) gives it, or an empty view.
    [[nodiscard]] constexpr std::string_view value(Slot s) const {
        return text(s).value_or(std::string_view{});
    }
    // seq's integer, 0 unless seq is one.
    [[nodiscard]] constexpr std::int64_t seq() const noexcept { return seq_; }
    [[nodiscard]] constexpr bool unknown_key() const noexcept { return unknown_; }
    // The text's bytes held: its capacity, its terminator apart.
    [[nodiscard]] constexpr std::size_t bytes() const noexcept { return text_.capacity(); }
    // The 11 fields as digest frames them; views of this header's text.
    [[nodiscard]] constexpr std::array<digest::Field, field_count> fields() const {
        std::array<digest::Field, field_count> out{};
        for (std::size_t i = 0; i < field_count; ++i) {
            out.at(i) = text(static_cast<Slot>(i));
        }
        return out;
    }

    // The stored line into out, replaced only on success: the present
    // slots as canonical JSON, seq an integer. A slot of another type, or
    // an integer but seq's, is HeaderError syntax; text the walk refuses,
    // ParseError.
    template <typename S>
    constexpr void line(S& out) const {
        std::size_t nodes = 1;
        std::size_t bytes = 0;
        for (std::size_t i = 0; i < slot_count; ++i) {
            const Place& p = places_.at(i);
            const bool integer = p.form == Form::integer && i == detail::index(Slot::seq);
            if (p.form == Form::other || (p.form == Form::integer && !integer)) {
                throw HeaderError{Code::syntax};
            }
            if (p.form != Form::absent) {
                nodes += 2;
                bytes += slot_names.at(i).size() + (integer ? 0 : p.size);
            }
        }
        json::Basic<A> tree(get_allocator());
        tree.reserve(nodes, bytes);
        json::Writer<A> w(tree);
        w.open(json::Kind::object);
        for (const Slot s : line_order) {
            if (!present(s)) {
                continue;
            }
            w.string(slot_names[detail::index(s)]);
            if (form(s) == Form::integer) {
                w.integer(seq_);
            } else {
                w.string(value(s));
            }
        }
        w.close();
        canon::canonical(tree, out);
    }

private:
    struct Place {
        std::uint32_t at = 0;
        std::uint32_t size = 0;
        Form form = Form::absent;
    };

    Text text_;
    std::array<Place, slot_count> places_{};
    std::int64_t seq_ = 0;
    bool unknown_ = false;
};

#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Header<memory::Allocator<char>>) == 200,
              "the header's size is pinned: its text, 40, the 12 places, 144, seq and the flag");
#endif

// The id over a header's fields, in place.
template <typename A>
constexpr detail::IdText id_of(const Header<A>& header) {
    const std::array<digest::Field, field_count> fields = header.fields();
    detail::IdText id;
    digest::entry_id(std::span<const digest::Field>(fields), id);
    return id;
}

// A stored line's header. Its value is parsed strictly and dropped before
// the header returns; the header's text is its one allocation beyond
// canon's.
template <typename A = std::allocator<char>>
constexpr Header<A> parse_line(std::string_view data, const A& allocator = A()) {
    json::Basic<A> value(allocator);
    canon::parse(data, value);
    if (value.kind(0) != json::Kind::object) {
        throw HeaderError{Code::syntax};
    }
    detail::PartsView parts;
    std::array<std::array<char, 20>, slot_count> digits{};
    const json::Index end = value.next(0);
    for (json::Index k = 1; k < end; k = value.next(k + 1)) {
        const std::optional<std::size_t> s = detail::slot_of(value.text(k));
        if (!s) {
            parts.unknown = true;
            continue;
        }
        const json::Index v = k + 1;
        switch (value.kind(v)) {
            case json::Kind::string:
                parts.forms.at(*s) = Form::text;
                parts.bytes.at(*s) = value.text(v);
                break;
            case json::Kind::integer:
                parts.forms.at(*s) = Form::integer;
                parts.bytes.at(*s) = detail::decimal(value.integer(v), digits.at(*s));
                if (*s == detail::index(Slot::seq)) {
                    parts.seq = value.integer(v);
                }
                break;
            default: parts.forms.at(*s) = Form::other; break;
        }
    }
    return Header<A>(parts, allocator);
}

// A header from the caller's fields: one allocation, its id computed over
// them unless given.
template <typename A = std::allocator<char>>
constexpr Header<A> make(const FieldsView& f, const A& allocator = A()) {
    detail::PartsView parts;
    std::array<char, 20> digits{};
    const auto put = [&parts](Slot s, const std::optional<std::string_view>& bytes, Form form) {
        if (bytes) {
            parts.forms.at(detail::index(s)) = form;
            parts.bytes.at(detail::index(s)) = *bytes;
        }
    };
    put(Slot::tag, f.tag, Form::text);
    put(Slot::scope, f.scope, Form::text);
    put(Slot::lane, f.lane, Form::text);
    put(Slot::seq, detail::decimal(f.seq, digits), Form::integer);
    put(Slot::parent, f.parent, Form::text);
    put(Slot::kind, f.kind, Form::text);
    put(Slot::writer, f.writer, Form::text);
    put(Slot::basis, f.basis, Form::text);
    put(Slot::at, f.at, Form::text);
    put(Slot::target, f.target, Form::text);
    put(Slot::content, f.content, Form::text);
    parts.seq = f.seq;
    detail::IdText computed;
    if (f.id) {
        put(Slot::id, f.id, Form::text);
    } else {
        std::array<digest::Field, field_count> fields = memory::each_value<digest::Field, field_count>();
        for (std::size_t i = 0; i < field_count; ++i) {
            if (parts.forms.at(i) != Form::absent) {
                fields.at(i) = parts.bytes.at(i);
            }
        }
        digest::entry_id(std::span<const digest::Field>(fields), computed);
        put(Slot::id, computed.view(), Form::text);
    }
    return Header<A>(parts, allocator);
}

namespace detail {

// Whether each present text matches pattern, compiled once over the
// header's allocator: two allocations, freed before it returns.
template <typename A, std::size_t N>
constexpr bool all_match(std::string_view pattern, const std::array<std::optional<std::string_view>, N>& texts,
                         const A& allocator) {
    const matcher::Basic<A> automaton = matcher::compile(pattern, allocator);
    for (const std::optional<std::string_view>& t : texts) {
        if (t && !matcher::hit(automaton, *t)) {
            return false;
        }
    }
    return true;
}

// Each present slot's type and syntax.
template <typename A>
constexpr bool syntax(const Header<A>& h) {
    for (std::size_t i = 0; i < slot_count; ++i) {
        const Form want = i == index(Slot::seq) ? Form::integer : Form::text;
        if (h.form(static_cast<Slot>(i)) != Form::absent && h.form(static_cast<Slot>(i)) != want) {
            return false;
        }
    }
    if (h.value(Slot::tag) != tag_value || !among(kind_names, h.value(Slot::kind)) ||
        !among(basis_names, h.value(Slot::basis)) || utc::check_at(h.value(Slot::at)) || h.seq() < 1 ||
        h.seq() > seq_max) {
        return false;
    }
    const A a = h.get_allocator();
    using One = std::array<std::optional<std::string_view>, 1>;
    using Three = std::array<std::optional<std::string_view>, 3>;
    return all_match(scope_syntax, One{h.text(Slot::scope)}, a) && all_match(lane_syntax, One{h.text(Slot::lane)}, a) &&
           all_match(writer_syntax, One{h.text(Slot::writer)}, a) &&
           all_match(id_syntax, Three{h.text(Slot::id), h.text(Slot::parent), h.text(Slot::target)}, a) &&
           (!h.present(Slot::content) || all_match(content_syntax, One{h.text(Slot::content)}, a));
}

}  // namespace detail

// The header's first fault in check's order, or none.
template <typename A>
constexpr std::optional<Code> try_check(const Header<A>& h) {
    if (h.unknown_key()) {
        return Code::unknown_key;
    }
    for (std::size_t i = 0; i < slot_count; ++i) {
        if (!optional_slot(static_cast<Slot>(i)) && !h.present(static_cast<Slot>(i))) {
            return Code::missing_key;
        }
    }
    for (std::size_t i = 0; i < field_count; ++i) {
        if (h.form(static_cast<Slot>(i)) == Form::text && h.value(static_cast<Slot>(i)).empty()) {
            return Code::empty;
        }
    }
    const std::string_view writer = h.value(Slot::writer);
    if (role(writer) == owner_role || h.value(Slot::lane) == owner_role) {
        return Code::owner;
    }
    if (!detail::syntax(h)) {
        return Code::syntax;
    }
    const std::string_view kind = h.value(Slot::kind);
    const std::string_view slot_role = role(writer);
    if (writer.find('/') != std::string_view::npos && (slot_role == "loop" || slot_role == "import")) {
        return Code::reserved;
    }
    if (h.value(Slot::lane) != slot_role) {
        return Code::lane;
    }
    if (slot_role == reflect_role && !among(reflect_kinds, kind)) {
        return Code::reflect_kind;
    }
    if ((h.seq() == 1) == h.present(Slot::parent)) {
        return Code::seq;
    }
    if (among(change_kinds, kind) && !h.present(Slot::target)) {
        return Code::target;
    }
    if ((kind == "forget") == h.present(Slot::content)) {
        return Code::content;
    }
    const detail::IdText id = id_of(h);
    if (id.view() != h.value(Slot::id)) {
        return Code::id_mismatch;
    }
    return std::nullopt;
}

// try_check, a fault thrown as HeaderError.
template <typename A>
constexpr void check(const Header<A>& h) {
    if (const std::optional<Code> fault = try_check(h)) {
        throw HeaderError{*fault};
    }
}

// Anything with the platform's now(): the platform itself, or a test's.
template <typename C>
concept Clock = requires(C& clock) {
    { clock.now() } -> std::same_as<utc::Millis>;
};

// A new entry's time: the clock, raised to the parent's if earlier, and to
// 1 ms past the target's if not later.
constexpr utc::Millis raise(utc::Millis clock, std::optional<utc::Millis> parent,
                            std::optional<utc::Millis> target) noexcept {
    utc::Millis t = clock;
    if (parent && t < *parent) {
        t = *parent;
    }
    if (target && t <= *target) {
        t = *target + std::chrono::milliseconds(1);
    }
    return t;
}

// A new entry's at from the clock's now() and the parent's and target's at,
// each absent where the entry has none; no allocation. An at that is none,
// or a time past year 9999, is HeaderError syntax.
template <Clock C>
constexpr std::array<char, utc::at_size> stamp(C& clock, std::optional<std::string_view> parent_at,
                                               std::optional<std::string_view> target_at) {
    const auto read = [](std::optional<std::string_view> at) -> std::optional<utc::Millis> {
        if (!at) {
            return std::nullopt;
        }
        const std::optional<utc::Millis> t = utc::parse_at(*at);
        if (!t) {
            throw HeaderError{Code::syntax};
        }
        return t;
    };
    const std::optional<utc::Millis> parent = read(parent_at);
    const std::optional<utc::Millis> target = read(target_at);
    const std::optional<std::array<char, utc::at_size>> text = utc::format_at(raise(clock.now(), parent, target));
    if (!text) {
        throw HeaderError{Code::syntax};
    }
    return *text;
}

}  // namespace qiven3::entry::header
