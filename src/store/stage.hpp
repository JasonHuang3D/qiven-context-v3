#pragma once

// stage: the shared validator's two parts as each store calls them, for an
// append, a forget or an import; the content hits; the maximum.
//
// check(platform, lines, at, allocator, scratch), before any lock, entry
// by entry: at forget, first each line read through the strict parser, a
// line it refuses EntryInvalid with the parser's code, and an entry whose
// kind is not forget StoreError forget_kind, its detail naming the entry;
// then part 1 alone. At append, part 1, then a forget refused EntryInvalid
// forget_path, then the secret check: a header hit refused EntryInvalid
// secret, the content's hits kept with their entry's index. At import,
// part 1 alone. It returns the entries, the hits and the bytes the lines
// add: each header line and body record with its LF.
//
// admit(view, scope, lane, expected, checked, limits, at, allocator),
// inside the store's transaction or lock, against its view: at append the
// hits settled first, in order, the first not cleared EntryInvalid secret:
// a hit passes when a live clearing of the entry's scope clears its pattern
// for the entry's content at the entry's at, unused, no header held but the
// entry's own, the call's earlier ones included, having that content. Then
// part 2 (validator), its first fault EntryInvalid with its code; then, at
// append alone, the maximum: the view's size and count with the call's
// added past the limits' maximum is StoreFull store_full, with the size and
// count the call would reach. It returns the new head and what the call
// adds. A HeadConflict, a guard and whole_record are the store's.
//
// At import, each line after check passes import_line(view, header,
// index, at) as it arrives, before its insert: the chain rules, parent and
// seq, against the view's head of its lane. Once every header is in,
// import_entry(view, header, content, index, at, allocator) reads each
// entry in the final view: part 2's other rules, target to map_evidence,
// where a revision whose target is forgotten, itself not, is
// forgotten_target; then a body held for a forgotten id forgotten_body; a
// content with no body, not forgotten, body_missing. same_heads compares
// the source's heads with those its entries give, in order: head_mismatch.
//
// Refusals are EntryInvalid with the entry's index and its id once part 1
// has read the header through (a body or content fault, part 2), never for
// secret (A7); the field's path for a content fault, a secret and
// forget_path (header.kind).
//
// Allocations. An entry's work is done on a scratch arena, which the
// caller's scratch() gives afresh for each line, over the same pages: at
// forget the line's value, four; part 1's (check's); at append the
// content's value, four with a body, the secret check's 24 and its hits'
// room for a first hit. Each is given back before the next line, so the
// scratch peaks at one entry's. On the caller's allocator, what check
// keeps: the entries' room, one; each entry's header, one, and body, one;
// the call's hits' room, one where it is short. admit: part 2's
// (validator's) and the view's, on its allocator; import_line none but the
// view's; import_entry validator's try_final's and the view's, on its
// allocator. Platform calls: part 1's
// wipes and each kept body's key's, each noexcept. stage logs nothing.
//
// thread: none; the entries, one (their keys' platform's).

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "entry/check.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/secret.hpp"
#include "entry/validator.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::stage {

namespace header = entry::header;
namespace secret = entry::secret;
namespace validator = entry::validator;
using Slot = header::Slot;
using Chars = memory::Allocator<char>;

template <platform::Platform P>
using Entry = entry::check::Entry<P, Chars>;

// The call a refusal names: its operation and the store's path.
struct AtView {
    Operation op = Operation::append;
    std::string_view path;
};

// A content hit: its entry's index in the call, its pattern and path.
struct Hit {
    std::uint32_t index = 0;
    secret::Hit hit;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Hit) == 120, "Hit's size is pinned: an index and secret's hit");
#endif

// What check admits: the entries, the content hits in order, and the bytes
// the lines add, each with its LF.
template <platform::Platform P>
struct Checked {
    explicit Checked(const Chars& allocator) noexcept : entries(allocator), hits(allocator) {}
    Vector<Entry<P>> entries;
    Vector<Hit> hits;
    std::uint64_t size = 0;
};

// What admit stages: the new head, and the bytes and header lines added.
struct Staged {
    Head head;
    std::uint64_t size = 0;
    std::uint64_t count = 0;
};
static_assert(sizeof(Staged) == 96, "Staged's size is pinned: a head and two counts");

// The view admit reads: part 2's, and the size and count held.
template <class V>
concept View = validator::View<V, Chars> && requires(const V& v) {
    { v.size() } -> std::same_as<std::uint64_t>;
    { v.count() } -> std::same_as<std::uint64_t>;
};

// The scratch: each call a fresh arena over the same pages, the previous
// one's blocks all given back.
template <class S>
concept Scratch = requires(S& s) {
    { s() } -> std::same_as<memory::Arena&>;
};

// The refusals, each written by stage.cpp.
[[noreturn]] void refuse(const AtView& at, std::size_t index, std::string_view id, std::string_view code,
                         std::string_view field = {}, std::string_view pattern = {});
[[noreturn]] void refuse_part1(const AtView& at, std::size_t index, std::string_view id,
                               const entry::check::Fault& fault);
[[noreturn]] void refuse_kind(const AtView& at, std::size_t index);
[[noreturn]] void refuse_full(const AtView& at, std::uint64_t size, std::uint64_t count, const Limits& limits);
// A call past validator's entries, or a sum past its type: MemoryError budget.
[[noreturn]] void too_large(std::size_t request);

namespace detail {

// a + b, a past its type MemoryError budget.
inline std::uint64_t sum(std::uint64_t a, std::uint64_t b) {
    if (a > UINT64_MAX - b) {
        too_large(SIZE_MAX);
    }
    return a + b;
}

// forget_kind: the line through the strict parser, then its kind.
inline void forget_kind(const AtView& at, std::size_t index, std::string_view line, const Chars& allocator) {
    canon::Basic<Chars> value(allocator);
    if (const std::optional<ParseCode> code = canon::try_parse(line, value)) {
        refuse_part1(at, index, {}, entry::check::detail::fault(entry::check::Part::parse, *code));
    }
    if (entry::kinds::detail::member(value, 0, "kind") != "forget") {
        refuse_kind(at, index);
    }
}

// Part 1 of one entry, as check_entry reads it, a fault refused.
template <platform::Platform P>
Entry<P> part1(P& platform, const AtView& at, std::size_t index, const EntryView& line, const Chars& allocator) {
    header::Header<Chars> h(allocator);
    std::optional<entry::body::Body<P, Chars>> b;
    const auto bind = [&](const entry::body::detail::RecordView& parts, const header::Header<Chars>& with) {
        b.emplace(parts.content, parts.id, digest::key_from_hex(platform, parts.key), allocator);
        try {
            entry::body::check(*b, with);
        } catch (const entry::body::BodyError&) {
            return false;
        }
        return true;
    };
    if (const std::optional<entry::check::Fault> f =
            entry::check::try_check(line.line, line.body, h, bind, allocator)) {
        const bool read = f->part == entry::check::Part::body || f->part == entry::check::Part::content;
        refuse_part1(at, index, read ? h.value(Slot::id) : std::string_view(), *f);
    }
    return Entry<P>(std::move(h), std::move(b));
}

// An admitted entry kept on the caller's allocator: its header made again
// from its fields, its id given, and its body from the record's parts.
template <platform::Platform P>
Entry<P> keep(P& platform, const Entry<P>& e, const EntryView& line, const Chars& allocator) {
    const header::Header<Chars>& h = e.header();
    header::FieldsView f;
    f.tag = h.value(Slot::tag);
    f.scope = h.value(Slot::scope);
    f.lane = h.value(Slot::lane);
    f.seq = h.seq();
    f.parent = h.text(Slot::parent);
    f.kind = h.value(Slot::kind);
    f.writer = h.value(Slot::writer);
    f.basis = h.value(Slot::basis);
    f.at = h.value(Slot::at);
    f.target = h.text(Slot::target);
    f.content = h.text(Slot::content);
    f.id = h.text(Slot::id);
    header::Header<Chars> kept = header::make(f, allocator);
    std::optional<entry::body::Body<P, Chars>> b;
    if (e.has_body() && line.body) {
        const entry::body::detail::RecordView parts = entry::body::detail::parts(*line.body);
        b.emplace(parts.content, parts.id, digest::key_from_hex(platform, parts.key), allocator);
    }
    return Entry<P>(std::move(kept), std::move(b));
}

// The secret check of one admitted entry: a header hit refused, the
// content's hits added to out.
template <platform::Platform P>
void secrets(const AtView& at, std::size_t index, const Entry<P>& e, Vector<Hit>& out, const Chars& scratch) {
    canon::Basic<Chars> value(scratch);
    if (e.has_body()) {
        canon::parse(std::string_view(e.body().content()), value);
    }
    secret::Hits<Chars> found(scratch);
    if (const std::optional<secret::SecretError> s = secret::try_check(e.header(), value, found)) {
        refuse(at, index, {}, "secret", s->path.view(), secret::name(s->pattern));
    }
    if (found.empty()) {
        return;
    }
    const std::optional<std::size_t> room = memory::add(out.size(), found.size());
    if (!room) {
        too_large(SIZE_MAX);
    }
    if (out.capacity() < *room) {
        out.reserve(*room);
    }
    for (const secret::Hit& h : found) {
        out.push_back(Hit{static_cast<std::uint32_t>(index), h});
    }
}

// Whether entry i's hit is cleared, unused.
template <class V, platform::Platform P>
bool cleared(V& view, std::span<const Entry<P>> entries, std::size_t i, std::string_view pattern) {
    const header::Header<Chars>& h = entries[i].header();
    const std::optional<std::string_view> content = h.text(Slot::content);
    if (!content || !view.cleared(h.value(Slot::scope), *content, pattern, h.value(Slot::at))) {
        return false;
    }
    for (std::size_t j = 0; j < i; ++j) {
        if (entries[j].header().text(Slot::content) == content) {
            return false;
        }
    }
    return !view.holds_content(*content);
}

}  // namespace detail

// Part 1 of each line, before any lock; a refusal thrown.
template <platform::Platform P, Scratch S>
Checked<P> check(P& platform, std::span<const EntryView> lines, const AtView& at, const Chars& allocator,
                 S& scratch) {
    if (at.op != Operation::append && at.op != Operation::forget && at.op != Operation::import_) {
        memory::fail_fast("stage::check: an operation that takes no entries");
    }
    if (lines.size() > validator::max_entries) {
        too_large(lines.size());
    }
    if (at.op == Operation::forget) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            detail::forget_kind(at, i, lines[i].line, Chars(scratch()));
        }
    }
    Checked<P> out(allocator);
    out.entries.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Chars work(scratch());
        const Entry<P> e = detail::part1(platform, at, i, lines[i], work);
        out.size = detail::sum(out.size, detail::sum(lines[i].line.size(), 1));
        if (const std::optional<std::string_view> body = lines[i].body) {
            out.size = detail::sum(out.size, detail::sum(body->size(), 1));
        }
        if (at.op == Operation::append) {
            if (e.header().value(Slot::kind) == "forget") {
                refuse(at, i, e.header().value(Slot::id), "forget_path", "header.kind");
            }
            detail::secrets(at, i, e, out.hits, work);
        }
        out.entries.push_back(detail::keep(platform, e, lines[i], allocator));
    }
    return out;
}

// The hits settled, part 2 and the maximum, inside the store's lock; a
// refusal thrown.
template <View V, platform::Platform P>
Staged admit(V& view, std::string_view scope, std::string_view lane, Expected expected, const Checked<P>& checked,
             const Limits& limits, const AtView& at, const Chars& allocator) {
    if (at.op != Operation::append && at.op != Operation::forget) {
        memory::fail_fast("stage::admit: an operation but append and forget");
    }
    const std::span<const Entry<P>> entries(checked.entries);
    for (const Hit& hit : checked.hits) {
        const std::string_view pattern = secret::name(hit.hit.pattern);
        if (!detail::cleared(view, entries, hit.index, pattern)) {
            refuse(at, hit.index, {}, "secret", hit.hit.path.view(), pattern);
        }
    }
    if (const std::optional<validator::RuleError> e =
            validator::try_validate(view, scope, lane, expected, entries, allocator)) {
        refuse(at, e->index, e->id.view(), validator::name(e->code));
    }
    Staged out;
    out.size = checked.size;
    out.count = entries.size();
    if (!entries.empty()) {
        const header::Header<Chars>& last = entries.back().header();
        out.head = Head{Id::of(last.value(Slot::id)), last.seq()};
    }
    if (at.op == Operation::append) {
        const std::uint64_t size = detail::sum(view.size(), out.size);
        const std::uint64_t count = detail::sum(view.count(), out.count);
        if (over_maximum(size, count, limits)) {
            refuse_full(at, size, count, limits);
        }
    }
    return out;
}

// One line of an import as it arrives, part 1 passed, before its insert:
// the chain rules (validator's), a fault refused.
template <class V>
    requires validator::View<V, Chars>
void import_line(V& view, const Header& h, std::size_t index, const AtView& at) {
    if (const std::optional<validator::Code> c = validator::try_chain(view, h)) {
        refuse(at, index, h.value(Slot::id), validator::name(*c));
    }
}

// One entry of an import in its final view, held there with its content
// or none: part 2's other rules (validator's), forgotten_target among
// them, then forgotten_body, then body_missing; a fault refused.
template <class V>
    requires validator::FinalView<V, Chars>
void import_entry(V& view, const Header& h, std::optional<std::string_view> content, std::size_t index,
                  const AtView& at, const Chars& allocator) {
    const std::string_view id = h.value(Slot::id);
    if (const std::optional<validator::Code> c = validator::try_final(view, h, content, allocator)) {
        refuse(at, index, id, validator::name(*c));
    }
    const bool forgotten = view.forgotten(id);
    if (content && forgotten) {
        refuse(at, index, id, "forgotten_body");
    }
    if (!content && !forgotten && h.text(Slot::content)) {
        refuse(at, index, id, "body_missing");
    }
}

// The source's heads against those its entries give, each in (scope,
// lane) order: the first that differs, or a count that does, refused
// head_mismatch, its index the source's and its id the source head's.
void same_heads(std::span<const LaneHead> source, std::span<const LaneHead> given, const AtView& at);

}  // namespace qiven3::store::stage
