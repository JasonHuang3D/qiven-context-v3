#pragma once

// write/entries: the entries file's reader, product. read takes the file's
// bytes (it opens nothing), one JSON object in any layout by canon's strict
// parser, and gives back its entries in order with their holes found, or
// refuses the whole file with an EntriesError: form or invalid, its why,
// the field's path (as entries[3].content.admit.mistake), the entry's
// index and name once read. build_content fills an entry's holes.
//
// The file, these members alone: form R; writer R; scope, required for a
// slot; episode {client, session}, required for a slot, refused for
// import; pieces O, 1 to max_pieces slugs each to {text} or an absolute
// {path}; entries R, 1 to max_slot_entries, max_import_entries for import.
// An entry: name O, a slug unique in the file; scope O, import's alone;
// kind R; basis R; target, an id or a placeholder, required on a change
// kind, refused on a kind with a record identity; content R, an object
// with no episode or clears. A hole: an item of refs or closes, or an entry
// reference's id, beginning with @ (@name: an earlier entry's, @item:,
// @lane:, @question:), and a repo, transcript or output reference's piece,
// in place of its digest, naming one of pieces; each piece named. Last,
// each content with stand-ins in its holes (a slot's episode copied in),
// under a stand-in header, passes entry/content's check, past 65,536 bytes
// too_large before it; a fault is invalid, its path under the content.
//
// Left to the commands: which writer and kinds each takes, @item:, @lane:
// and @question: against the fold, a path piece read, expires against the
// entry's at, the secret check.
//
// Allocations, all the arena's, each content's check in a work span of
// work_bytes taken once and given back; past it MemoryError budget. Under
// QIVEN3_FAULTS the check's arena asks hooks.work first. It logs nothing.
//
// thread: one (a File stays in the thread that read it)

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "entry/content_fields.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::write::entries {

inline constexpr std::string_view form_value = "qiven3-entries/1";
inline constexpr std::size_t max_bytes = std::size_t{16} << 20U;  // [proposal]
inline constexpr std::size_t max_slot_entries = 64;               // [proposal]
inline constexpr std::size_t max_import_entries = 2000;           // [proposal]
inline constexpr std::size_t max_piece_bytes = std::size_t{4} << 20U;  // [proposal]
inline constexpr std::size_t max_pieces = 256;       // [reading]
inline constexpr std::size_t max_path_bytes = 4096;  // [reading]
inline constexpr std::size_t name_chars = 64;        // a slug's, as content's
inline constexpr std::uint32_t none = UINT32_MAX;

enum class Code : std::uint8_t { form, invalid };
// content is invalid's, the content check's fault; the rest are form's.
enum class Why : std::uint8_t {
    too_large, json, type, unknown_member, not_allowed, missing, value,
    count, syntax, repeated, placeholder, piece, unused_piece, content
};
std::string_view name(Code code) noexcept;
std::string_view name(Why why) noexcept;

// The refusal, thrown by value, allocating nothing: canon's code for json;
// the check's fault, limit and value for invalid; entry none before one.
struct EntriesError {
    Code code = Code::form;
    Why why = Why::syntax;
    ParseCode parse = ParseCode::syntax;
    entry::content::Code fault = entry::content::Code::syntax;
    std::uint32_t limit = 0;
    std::uint32_t value = 0;
    memory::Inline<128> path{};
    std::uint32_t entry = none;
    memory::Inline<64> name{};
};

// A string of the file's value by its node; 0 for none.
using Text = json::Index;

// A hole in an entry's target or content.
enum class Hole : std::uint8_t { name, item, lane, question, piece };
struct Site {
    json::Index node = 0;  // the string, or a piece reference's object
    Hole hole = Hole::name;
    std::uint32_t named = none;  // @name:'s entry, or a piece's index
    std::uint8_t prefix = 0;     // a placeholder's prefix length: File::key
};
struct Span {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};
struct Entry {
    Text name = 0;
    Text scope = 0;  // the entry's, else the file's
    Text kind = 0;
    Text basis = 0;
    Text target = 0;
    std::uint32_t target_site = none;  // its site when a placeholder
    json::Index content = 0;
    Span sites;  // the content's holes, in document order
};
struct Piece {
    Text name = 0;
    Text value = 0;  // the text, or the path
    bool path = false;
};

namespace detail {
struct Reader;
}  // namespace detail

// The tests' hook: under QIVEN3_FAULTS each content check's arena asks
// work first.
struct Hooks {
#if defined(QIVEN3_FAULTS)
    memory::Countdown* work = nullptr;
#endif
};

// The file read, its texts in its own value, so it outlives the bytes; all
// of it the arena's. A span or index past its list is a caller's mistake:
// fail_fast.
class File {
public:
    explicit File(memory::Arena& arena) noexcept;

    [[nodiscard]] std::string_view text(Text t) const noexcept;
    [[nodiscard]] std::string_view writer() const noexcept { return text(writer_); }
    [[nodiscard]] std::string_view scope() const noexcept { return text(scope_); }
    [[nodiscard]] bool import() const noexcept { return writer() == "import"; }
    [[nodiscard]] json::Index episode() const noexcept { return episode_; }  // 0 for none
    [[nodiscard]] const json::Value& value() const noexcept { return value_; }
    [[nodiscard]] std::span<const Entry> entries() const noexcept { return entries_; }
    [[nodiscard]] std::span<const Piece> pieces() const noexcept { return pieces_; }
    [[nodiscard]] std::span<const Site> sites() const noexcept { return sites_; }
    [[nodiscard]] std::span<const Site> sites(const Entry& e) const;
    // What follows a placeholder's prefix; empty for a piece reference.
    [[nodiscard]] std::string_view key(const Site& s) const {
        return s.hole == Hole::piece ? std::string_view{} : text(s.node).substr(s.prefix);
    }

private:
    friend File read(std::string_view bytes, memory::Arena& arena, Hooks hooks);
    friend struct detail::Reader;

    json::Value value_;
    Text writer_ = 0;
    Text scope_ = 0;
    json::Index episode_ = 0;
    memory::Vector<Entry> entries_;
    memory::Vector<Piece> pieces_;
    memory::Vector<Site> sites_;
    memory::Vector<std::uint32_t> by_name_;  // named entries, by name
};

// The file of bytes, or EntriesError; MemoryError budget past the arena.
[[nodiscard]] File read(std::string_view bytes, memory::Arena& arena, Hooks hooks = {});

// Entry e's content into out, empty, its sites filled in order (a piece
// reference's piece dropped for its digest) and a slot's episode appended;
// a fill count other than the sites' is fail_fast. Allocations, out's.
void build_content(const File& file, std::size_t e, std::span<const std::string_view> fills, json::Value& out);

}  // namespace qiven3::write::entries
