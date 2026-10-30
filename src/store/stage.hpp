#pragma once

// stage: the store's steps of an append, a forget or an import: part 1 of
// the shared validator, each entry kept; the store's own order; the
// maximum. The rules of cognition are an admit hook's (the contract's
// Admit and AdmitImport), the product's; the store runs none of them.
//
// read(platform, lines, at, allocator, scratch, admit), before any lock,
// entry by entry: the hook's given before the first line; part 1, a fault
// refused; the hook's read once part 1 has read the header; the entry kept
// on the caller's allocator, then the hook's kept. It returns the entries
// and the bytes the lines add: each header line and body record with its
// LF.
//
// staged(view, checked, limits, at), inside the store's transaction or
// lock, after the hook's admit: at append alone, the maximum: the view's
// size and count with the call's added past the limits' maximum is
// StoreFull store_full, with the size and count the call would reach. It
// returns the new head and what the call adds. With the call's scope, lane
// and the head the store read, staged first runs in_order, the store's own
// order over its keys. A HeadConflict, a guard and whole_record are the
// store's.
//
// Refusals are EntryInvalid with the entry's index and its id once part 1
// has read the header through (a body or content fault, the order); the
// field's path for a content fault.
//
// Allocations. An entry's part 1 is done on a scratch arena, which the
// caller's scratch() gives afresh over the same pages, given back before
// the next; the hook's are its own (each hook says). On the caller's
// allocator, what read keeps: the entries' room, one; each entry's header,
// one, and body, one. staged and in_order none but the view's. Platform
// calls: part 1's wipes and each kept body's key's, each noexcept. stage
// logs nothing.
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
#include "entry/validator.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/digest.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::stage {

namespace header = entry::header;
namespace validator = entry::validator;
using Slot = header::Slot;
using Chars = memory::Allocator<char>;

template <platform::Platform P>
using Entry = entry::check::Entry<P, Chars>;

// The call a refusal names: its operation and the store's path.
using AtView = At;

// What read admits: the entries, and the bytes the lines add, each with
// its LF.
template <platform::Platform P>
struct Checked {
    explicit Checked(const Chars& allocator) noexcept : entries(allocator) {}
    Vector<Entry<P>> entries;
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

}  // namespace detail

// Part 1 of each line, before any lock, with the admit hook's given, read
// and kept at their places; a refusal thrown.
template <platform::Platform P, Scratch S, class A>
Checked<P> read(P& platform, std::span<const EntryView> lines, const AtView& at, const Chars& allocator, S& scratch,
                A& admit) {
    if (at.op != Operation::append && at.op != Operation::forget && at.op != Operation::import_) {
        memory::fail_fast("stage::check: an operation that takes no entries");
    }
    if (lines.size() > validator::max_entries) {
        too_large(lines.size());
    }
    admit.given(at, lines, allocator, scratch);
    Checked<P> out(allocator);
    out.entries.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        {
            const Entry<P> e = detail::part1(platform, at, i, lines[i], Chars(scratch()));
            out.size = detail::sum(out.size, detail::sum(lines[i].line.size(), 1));
            if (const std::optional<std::string_view> body = lines[i].body) {
                out.size = detail::sum(out.size, detail::sum(body->size(), 1));
            }
            admit.read(at, i, e.header());
            out.entries.push_back(detail::keep(platform, e, lines[i], allocator));
        }
        admit.kept(at, i, out.entries.back(), allocator, scratch);
    }
    return out;
}

// What the call stages, and at append the maximum: the store's, after the
// admit hook; a refusal thrown.
template <View V, platform::Platform P>
Staged staged(V& view, const Checked<P>& checked, const Limits& limits, const AtView& at) {
    const std::span<const Entry<P>> entries(checked.entries);
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

// The store's own order over its keys, under the head now it read: each
// entry on the call's scope and lane, its parent the lane's head and its
// seq one past it, the one before it in the call then the head; the first
// out of order refused parent or seq. An admit hook's rules run first.
template <platform::Platform P>
void in_order(std::string_view scope, std::string_view lane, std::optional<Head> now,
              std::span<const Entry<P>> entries, const AtView& at) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const header::Header<Chars>& h = entries[i].header();
        const std::string_view id = h.value(Slot::id);
        const std::optional<std::string_view> head =
            now ? std::optional<std::string_view>(now->id.view()) : std::nullopt;
        if (h.value(Slot::scope) != scope || h.value(Slot::lane) != lane || h.text(Slot::parent) != head) {
            refuse(at, i, id, "parent");
        }
        if (h.seq() < 1 || h.seq() - 1 != (now ? now->seq : 0)) {
            refuse(at, i, id, "seq");
        }
        now = Head{Id::of(id), h.seq()};
    }
}

// The store's step after the admit hook: its own order, then staged.
template <View V, platform::Platform P>
Staged staged(V& view, const Checked<P>& checked, const Limits& limits, const AtView& at, std::string_view scope,
              std::string_view lane, const std::optional<Head>& now) {
    in_order(scope, lane, now, std::span<const Entry<P>>(checked.entries), at);
    return staged(view, checked, limits, at);
}

}  // namespace qiven3::store::stage
