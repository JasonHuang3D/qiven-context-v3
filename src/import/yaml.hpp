#pragma once

// import/yaml: the reader of the flat YAML subset of the system v3 imports
// from, strictly, product. read_flat reads a file's bytes into its fields
// in document order, each a key and a text, or a key and a list of texts,
// or refuses the file with a YamlError naming its first faulty line. The
// meaning is the source system's, not YAML's: a quoted scalar is the text
// between its quotes, with no escape decoded; a plain scalar is the rest of
// the line, its spaces trimmed at both ends, " #" kept as text.
//
// The four forms, each at column 0, a line ending at LF:
//   key: value     a text; key is [a-z0-9_]+, then ": " and the value;
//   key:           a list, whose items follow;
//   - item         an item of the last list opened, comments and empty
//                  lines between kept in it; a text field closes the list;
//   # comment      a whole-line comment; and the empty line.
// A value is "..." or '...' holding neither its own quote nor a backslash,
// or a plain scalar that does not open with a quote and that the source
// system would not read as a boolean, a null or a number (true, false,
// null, none in any case, ~, or a decimal number as a float's text reads:
// a sign, digits with single underscores between, a point, an exponent;
// inf, infinity and nan in any case).
//
// Refused, by code: utf8, a byte that is not strict UTF-8; control, a tab
// or a CR; indented, a line opening with a space, a line of spaces too;
// form, a line none of the forms; duplicate_key, a key a second time;
// outside_list, an item with no list open; quote, a quoted scalar unclosed
// or holding its own quote or a backslash; typed, a plain scalar the source
// system would type; too_large, a file over max_bytes, line 0. A file's
// first faulty line names the error, and within a line the codes are read
// in the order above; a key's second line is its fault's line.
//
// Fields holds its keys and texts in a text of its own, each field and
// item a place in it, so it outlives the bytes it was read from; its text
// and two vectors are the arena's. Allocations: read_flat at most four,
// the text's, reserved for the file's bytes, the fields', the items' and
// the keys' order for the duplicate check, each reserved in one from a
// count of the lines' first bytes, the order given back before it returns.
// A request past the arena is MemoryError budget. It logs nothing.
//
// thread: one (a Fields stays in the thread that read it)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "memory/fail.hpp"
#include "memory/memory.hpp"

namespace qiven3::import::yaml {

// A file's bound in bytes.
inline constexpr std::size_t max_bytes = 65536;

enum class Code : std::uint8_t { utf8, control, indented, form, duplicate_key, outside_list, quote, typed, too_large };
std::string_view name(Code code) noexcept;

// The refusal: its code and the line, from 1, or 0 for the whole file. A
// plain value, thrown by value; it allocates nothing.
struct YamlError {
    Code code;
    std::size_t line;
};

// A place in Fields' text: its first byte and its size.
struct Place {
    std::uint32_t at = 0;
    std::uint32_t size = 0;
};

// One field: its key, its line, and a text, or a list of items.
struct Field {
    Place key;
    Place text;               // a text's; none for a list
    std::uint32_t line = 0;   // its key's
    std::uint32_t first = 0;  // a list's first item among Fields' items
    std::uint32_t count = 0;  // a list's items
    bool list = false;
};

// The fields, in document order. Asking past them is a caller's mistake:
// fail_fast.
class Fields {
public:
    explicit Fields(memory::Arena& arena) noexcept
        : fields_(memory::Allocator<Field>(arena)), items_(memory::Allocator<Place>(arena)),
          text_(memory::Allocator<char>(arena)) {}

    [[nodiscard]] std::size_t size() const noexcept { return fields_.size(); }
    [[nodiscard]] bool empty() const noexcept { return fields_.empty(); }
    [[nodiscard]] std::string_view key(std::size_t i) const noexcept { return view(at(i).key); }
    [[nodiscard]] std::size_t line(std::size_t i) const noexcept { return at(i).line; }
    [[nodiscard]] bool list(std::size_t i) const noexcept { return at(i).list; }
    // A text's text; empty for a list.
    [[nodiscard]] std::string_view text(std::size_t i) const noexcept { return view(at(i).text); }
    // A list's items, in order; none for a text.
    [[nodiscard]] std::size_t count(std::size_t i) const noexcept { return at(i).count; }
    [[nodiscard]] std::string_view item(std::size_t i, std::size_t k) const noexcept {
        const Field& field = at(i);
        if (k >= field.count) {
            memory::fail_fast("yaml: an item past its list's");
        }
        return view(items_[field.first + k]);
    }
    // The field of a key, or none.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view key) const noexcept;

private:
    friend Fields read_flat(std::string_view bytes, memory::Arena& arena);

    [[nodiscard]] const Field& at(std::size_t i) const noexcept {
        if (i >= fields_.size()) {
            memory::fail_fast("yaml: a field past the file's");
        }
        return fields_[i];
    }
    // A key's or a value's place, its bytes copied into the text.
    Place keep(std::string_view part) {
        const Place place{static_cast<std::uint32_t>(text_.size()), static_cast<std::uint32_t>(part.size())};
        text_.append(part);
        return place;
    }
    [[nodiscard]] std::string_view view(Place place) const noexcept {
        return std::string_view(text_).substr(place.at, place.size);
    }

    memory::Vector<Field> fields_;
    memory::Vector<Place> items_;
    memory::String text_;
};

// The fields of a file's bytes, or YamlError; MemoryError budget past the
// arena.
[[nodiscard]] Fields read_flat(std::string_view bytes, memory::Arena& arena);

}  // namespace qiven3::import::yaml
