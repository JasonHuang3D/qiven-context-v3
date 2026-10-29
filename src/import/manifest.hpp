#pragma once

// import/manifest: the reader of the import's quote manifest, product.
// read_manifest reads the manifest's bytes, JSON read by canon's strict
// parser in any layout, into its typed parts, or refuses the manifest with
// a ManifestError naming the field's path, and the entry's id once read.
// verbatim says whether a quote's words are found in its document's bytes,
// each line break and the spaces and tabs after it read as one space; a
// quote spanning lines matches so, the owner's own quotation marks are
// bytes like any other, and a quote whose spelling was corrected is
// verbatim to the document, its note kept beside it.
//
// The form, every member checked, any other refused by name:
//   manifest     R, a text of 64; written_by O, a text of 64;
//   records      R, v2's repository's name;
//   documents    R, 1 to 64 members, each a name to {repo, path, revision},
//                every repo one name;
//   rulings      R, up to 64 names of documents, none twice;
//   entries      R, 1 to 4,096: id, kind, basis, scope, band, band_basis O,
//                doc, section, ordinal, source (1 to 8 of {doc, section}
//                or {entry}), note O, also_in O (up to 8 of {doc, section,
//                words O, part O}); a direction's words 2,000, a claim's
//                claim 1,000, a decision's decides 500 and reasons O 8 x
//                300, in characters;
//   goal         R: kind goal, basis claimed, band core, scope, band_basis
//                O, doc, section, objective 500, boundary O 500,
//                test_questions O 8 x 300, source 1 to 8 directions by
//                {entry}, source_reasons O (an entry of source to a text),
//                seed_retire_reason O, seed_retire_reason_source O, when O;
//   person       R: kind decision, basis claimed, scope person, band core,
//                band_basis O, scope_basis O, status O, decides 500,
//                reasons O 8 x 300, decides_source O, source O,
//                reasons_source O (a list of sections per reason), note O;
//   replacements O, up to 256: doc, line, in_section, text, replaced and
//                replaced_by {doc, section}, decisions O {from, to},
//                heading_line O, judge O;
//   amendments   O, up to 256: doc, line, in_section, text, what.
// A name is [a-z0-9][a-z0-9-]*, at most 64; a repository's name 1 to 100
// of [A-Za-z0-9._-], neither "." nor ".."; a path relative, '/' between
// segments, none empty, "." or "..", no '\', ':' or control, at most 240;
// a revision 4 to 40 lowercase hex; a section 1 to 32 of [A-Za-z0-9.-];
// an id [a-z0-9][a-z0-9.-]*, at most 64; a scope project:<name>; an
// ordinal 1 to 9,999, a line 1 to 10,000,000; a note or any other text
// 500 characters, a document's line 2,000. A text is never empty.
//
// Refused, by code: too_large, bytes past max_bytes; json, bytes canon's
// parser refuses, its code kept; missing, a required member absent; type,
// a member of another JSON kind; bound, a text or a list past its bound
// or empty, an integer outside its range; form, a text not in its form;
// value, a kind, basis, band or scope its place does not take, a
// direction's band other than high, reasons_source not one list per
// reason; unknown_field, a member the form does not hold; unknown_document,
// a doc or a rulings name no documents member names; unknown_entry, an
// entry reference to no entry, or a goal's source or source_reasons key
// naming no direction of its source; duplicate, an id, a rulings name or
// an entry's (doc, section, ordinal) a second time, the later named;
// unused_document, a document nothing names; two_repositories, a repo
// other than the first document's; not_verbatim, check_verbatim's. The
// members are read in the form's order above, each object's names first;
// then ids, entry references and the documents' use.
//
// Allocations, all the arena's: canon's parse three, one dropped; then
// eleven vectors, each reserved once from a count of the value's nodes. A
// request past the arena is MemoryError budget. It logs nothing.
//
// thread: one (a Manifest stays in the thread that read it)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utf.hpp"

namespace qiven3::import::manifest {

// The manifest's bound in bytes [proposal].
inline constexpr std::size_t max_bytes = std::size_t{4} << 20U;
inline constexpr std::size_t max_documents = 64;
inline constexpr std::size_t max_entries = 4096;
inline constexpr std::size_t max_sources = 8;
inline constexpr std::size_t max_words = 2000;

enum class Code : std::uint8_t {
    too_large,
    json,
    missing,
    type,
    bound,
    form,
    value,
    unknown_field,
    unknown_document,
    unknown_entry,
    duplicate,
    unused_document,
    two_repositories,
    not_verbatim
};
std::string_view name(Code code) noexcept;

// The refusal: its code, canon's code for json, the field's path (as
// entries[3].source[1].entry; empty for the whole manifest) and the entry's
// id once read. A plain value, thrown by value; it allocates nothing.
struct ManifestError {
    Code code;
    ParseCode parse = ParseCode::syntax;
    memory::Inline<128> path;
    memory::Inline<64> entry;
};

// A string of the value, by its node; 0 for none (the root is no string).
using Text = json::Index;

// A run of a Manifest's list: its first item and its count.
struct Span {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

enum class Kind : std::uint8_t { direction, decision, claim };

struct Document {
    Text name = 0;
    Text repo = 0;
    Text path = 0;
    Text revision = 0;
};

// A source item: a document's section, or an entry by id.
struct Source {
    Text doc = 0;
    Text section = 0;
    Text entry = 0;
};

struct Also {
    Text doc = 0;
    Text section = 0;
    Text words = 0;
    bool part = false;
};

// A key and its text: a goal's source_reasons member.
struct Pair {
    Text key = 0;
    Text text = 0;
};

struct Entry {
    Kind kind = Kind::direction;
    Text id = 0;
    Text basis = 0;
    Text scope = 0;
    Text band = 0;
    Text band_basis = 0;
    Text doc = 0;
    Text section = 0;
    Text text = 0;  // a direction's words, a claim's claim, a decision's decides
    Text note = 0;
    std::int64_t ordinal = 0;
    Span reasons;   // texts
    Span sources;   // sources
    Span also;      // alsos
};

struct Goal {
    Text scope = 0;
    Text band_basis = 0;
    Text doc = 0;
    Text section = 0;
    Text objective = 0;
    Text boundary = 0;
    Text seed_retire_reason = 0;
    Text when = 0;
    Source seed_retire_reason_source;
    Span test_questions;  // texts
    Span sources;         // sources
    Span source_reasons;  // pairs
};

struct Person {
    Text band_basis = 0;
    Text scope_basis = 0;
    Text status = 0;
    Text decides = 0;
    Text note = 0;
    Span reasons;         // texts
    Span decides_source;  // sources
    Span sources;         // sources
    Span reasons_source;  // groups, each a span of sources
};

struct Replacement {
    Text doc = 0;
    Text in_section = 0;
    Text text = 0;
    Text heading_line = 0;
    Text judge = 0;
    Text from = 0;
    Text to = 0;
    std::int64_t line = 0;
    Source replaced;
    Source replaced_by;
};

struct Amendment {
    Text doc = 0;
    Text in_section = 0;
    Text text = 0;
    Text what = 0;
    std::int64_t line = 0;
};

namespace detail {
struct Reader;
}  // namespace detail

// The manifest read, its texts in its own value, so it outlives the bytes;
// all of it the arena's. A span past its list is a caller's mistake:
// fail_fast.
class Manifest {
public:
    explicit Manifest(memory::Arena& arena) noexcept;

    [[nodiscard]] std::string_view text(Text t) const noexcept;
    [[nodiscard]] std::string_view form() const noexcept { return text(form_); }
    [[nodiscard]] std::string_view written_by() const noexcept { return text(written_by_); }
    [[nodiscard]] std::string_view records() const noexcept { return text(records_); }
    // The documents' one repository.
    [[nodiscard]] std::string_view repository() const noexcept { return text(documents_.front().repo); }

    [[nodiscard]] std::span<const Document> documents() const noexcept { return documents_; }
    [[nodiscard]] std::span<const std::uint32_t> rulings() const noexcept { return rulings_; }  // documents
    [[nodiscard]] std::span<const Entry> entries() const noexcept { return entries_; }
    [[nodiscard]] const Goal& goal() const noexcept { return goal_; }
    [[nodiscard]] const Person& person() const noexcept { return person_; }
    [[nodiscard]] std::span<const Replacement> replacements() const noexcept { return replacements_; }
    [[nodiscard]] std::span<const Amendment> amendments() const noexcept { return amendments_; }

    [[nodiscard]] std::span<const Text> texts(Span s) const noexcept { return part(texts_, s); }
    [[nodiscard]] std::span<const Source> sources(Span s) const noexcept { return part(sources_, s); }
    [[nodiscard]] std::span<const Also> alsos(Span s) const noexcept { return part(alsos_, s); }
    [[nodiscard]] std::span<const Pair> pairs(Span s) const noexcept { return part(pairs_, s); }
    [[nodiscard]] std::span<const Span> groups(Span s) const noexcept { return part(groups_, s); }

    // A document by its name, an entry by its id, or none.
    [[nodiscard]] std::optional<std::size_t> document(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::size_t> entry(std::string_view id) const noexcept;

private:
    friend Manifest read_manifest(std::string_view bytes, memory::Arena& arena);
    friend struct detail::Reader;

    template <typename T>
    static std::span<const T> part(const memory::Vector<T>& all, Span s) noexcept {
        if (s.first > all.size() || s.count > all.size() - s.first) {
            memory::fail_fast("manifest: a span past its list's");
        }
        return std::span<const T>(all).subspan(s.first, s.count);
    }

    json::Value value_;
    Text form_ = 0;
    Text written_by_ = 0;
    Text records_ = 0;
    memory::Vector<Document> documents_;
    memory::Vector<std::uint32_t> rulings_;
    memory::Vector<Entry> entries_;
    memory::Vector<std::uint32_t> by_id_;  // entries, in their ids' order
    memory::Vector<Text> texts_;
    memory::Vector<Source> sources_;
    memory::Vector<Also> alsos_;
    memory::Vector<Pair> pairs_;
    memory::Vector<Span> groups_;
    memory::Vector<Replacement> replacements_;
    memory::Vector<Amendment> amendments_;
    Goal goal_;
    Person person_;
};

// The manifest of bytes, or ManifestError; MemoryError budget past the
// arena.
[[nodiscard]] Manifest read_manifest(std::string_view bytes, memory::Arena& arena);

// Whether words, not empty, are a substring of document read with each LF
// and the spaces and tabs after it as one space. It allocates nothing.
[[nodiscard]] bool verbatim(std::string_view words, std::string_view document) noexcept;

// A direction entry's words checked against its document's bytes:
// not_verbatim naming the entry otherwise. Another kind, or an entry past
// the manifest's, is a caller's mistake: fail_fast.
void check_verbatim(const Manifest& manifest, std::size_t entry, std::string_view document);

}  // namespace qiven3::import::manifest
