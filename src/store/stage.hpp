#pragma once

// stage: the store's steps of an append, a forget or an import: each
// entry's keys from the admit hook, kept; the store's own guards and order
// over them; the maximum. The rules of cognition are an admit hook's (the
// contract's Admit and AdmitImport), the product's; at append and forget
// the store runs none of them and reads no line. Its own guards run
// whatever hook it is given, so that it never commits a state its verify
// or open refuses. Until the import takes keys, part1 here reads an
// imported or verified line through the shared validator's part 1.
//
// read(platform, lines, at, allocator, scratch, admit), before any lock,
// entry by entry: the hook's given before the first line; then each line's
// keys from the hook's read; the store's bounds over them: id, parent and
// target at most Id's capacity, scope and lane at most their bytes, else
// EntryInvalid too_long, the key's name as its field (a seq below 1 is
// next's: seq); a body key with no record body_missing, a record with no
// body key body_mismatch; at forget a kind but forget StoreError
// forget_kind, its detail naming the entry, at append a forget
// EntryInvalid forget_path (header.kind); the keys kept, then the hook's
// kept. It returns the kept entries and the bytes the lines add: each
// header line and body record with its LF.
//
// arrives(view, header, index, at), at import as each line arrives, before
// the hook's arrived: its parent the view's head of its lane and its seq
// one past it, refused parent or seq. erasable(entries, erased, at), at
// forget once the store has found what it erases: each forget's target
// held in the call's scope with its body, none twice, the first not
// refused EntryInvalid target. settles(header, ...), at import once every
// line is in, after the hook's settled: a target not held, target; a body
// of a forgotten entry, forgotten_body; a content with no body, not
// forgotten, body_missing.
//
// staged(view, checked, limits, at), inside the store's transaction or
// lock, after the hook's admit: at append alone, the maximum: the view's
// size and count with the call's added past the limits' maximum is
// StoreFull store_full, with the size and count the call would reach. It
// returns the new head and what the call adds. With the call's scope, lane
// and the head the store read, staged first runs in_order, the store's own
// order over its keys; guarded the same, at append then targets, each
// entry's target held in the call's scope (the view's held_in), the
// store's or an earlier one of the call's, refused target. A HeadConflict
// and a guard are the store's.
//
// Refusals are EntryInvalid with the entry's index and its id key (none
// where the id is past its bound); part1's with its id once it has read
// the header through (a body or content fault), the field's path for a
// content fault.
//
// Allocations. read: on the caller's allocator, the kept entries' room,
// one; the hook's are its own (each hook says). part1 on the arena it is
// given. staged, in_order, arrives, erasable, settles and targets none but
// the view's. Platform calls: part1's wipes and its body's key's, each
// noexcept; none at append and forget. stage logs nothing.
//
// thread: none; the kept entries, one.

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
#include "store/keys.hpp"

namespace qiven3::store::stage {

namespace header = entry::header;
namespace validator = entry::validator;
using Slot = header::Slot;
using Chars = memory::Allocator<char>;

template <platform::Platform P>
using Entry = entry::check::Entry<P, Chars>;

// The call a refusal names: its operation and the store's path.
using store::AtView;

// What the store keeps of an entry: its keys owned, its kind, line and
// body record as views of the call's (valid for the call, the kind the
// hook's until its release).
struct KeptView {
    Id id;
    memory::Inline<scope_bytes> scope;
    memory::Inline<lane_bytes> lane;
    std::string_view kind;
    std::int64_t seq = 0;
    std::optional<Id> parent, target;
    bool body = false;
    std::string_view line;
    std::optional<std::string_view> record;

    // Its keys, views of it.
    [[nodiscard]] KeysView keys() const noexcept {
        const auto text = [](const std::optional<Id>& i) {
            return i ? std::optional<std::string_view>(i->view()) : std::nullopt;
        };
        return KeysView{id.view(), scope.view(), lane.view(), kind, seq, text(parent), text(target), body};
    }
};

// What read admits: the kept entries, and the bytes the lines add, each
// with its LF.
struct Checked {
    explicit Checked(const Chars& allocator) noexcept : kept(allocator) {}
    Vector<KeptView> kept;
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
// forget_kind's refusal, its detail naming the entry.
[[noreturn]] void refuse_kind(const AtView& at, std::size_t index);
// A call past max_call_entries, or a sum past its type: MemoryError budget.
[[noreturn]] void too_large(std::size_t request);

namespace detail {

// a + b, a past its type MemoryError budget.
inline std::uint64_t sum(std::uint64_t a, std::uint64_t b) {
    if (a > UINT64_MAX - b) {
        too_large(SIZE_MAX);
    }
    return a + b;
}

// One entry after now on its lane: its parent the head, refused parent,
// and its seq one past it, refused seq.
inline void next(std::string_view id, std::optional<std::string_view> parent, std::int64_t seq,
                 const std::optional<Head>& now, std::size_t index, const AtView& at) {
    if (parent != (now ? std::optional<std::string_view>(now->id.view()) : std::nullopt)) {
        refuse(at, index, id, "parent");
    }
    if (seq < 1 || seq - 1 != (now ? now->seq : 0)) {
        refuse(at, index, id, "seq");
    }
}
inline void next(const header::Header<Chars>& h, const std::optional<Head>& now, std::size_t index,
                 const AtView& at) {
    next(h.value(Slot::id), h.text(Slot::parent), h.seq(), now, index, at);
}

// The store's bounds over one entry's keys and its record, then its kind
// at its operation; the entry kept.
inline KeptView keep(const KeysView& k, const EntryView& line, std::size_t index, const AtView& at) {
    const std::string_view id = Id::fits(k.id) ? k.id : std::string_view();
    const auto bound = [&](bool fits, std::string_view field) {
        if (!fits) {
            refuse(at, index, id, "too_long", field);
        }
    };
    bound(Id::fits(k.id), "id");
    bound(k.scope.size() <= scope_bytes, "scope");
    bound(k.lane.size() <= lane_bytes, "lane");
    bound(!k.parent || Id::fits(*k.parent), "parent");
    bound(!k.target || Id::fits(*k.target), "target");
    if (k.body && !line.body) {
        refuse(at, index, id, "body_missing");
    }
    if (!k.body && line.body) {
        refuse(at, index, id, "body_mismatch");
    }
    if (at.op == Operation::forget && k.kind != "forget") {
        refuse_kind(at, index);
    }
    if (at.op == Operation::append && k.kind == "forget") {
        refuse(at, index, id, "forget_path", "header.kind");
    }
    KeptView out;
    out.id = Id::of(k.id);
    out.scope = memory::Inline<scope_bytes>(k.scope);
    out.lane = memory::Inline<lane_bytes>(k.lane);
    out.kind = k.kind;
    out.seq = k.seq;
    if (k.parent) {
        out.parent = Id::of(*k.parent);
    }
    if (k.target) {
        out.target = Id::of(*k.target);
    }
    out.body = k.body;
    out.line = line.line;
    out.record = line.body;
    return out;
}

// A kept entry's optional id as a view.
[[nodiscard]] inline std::optional<std::string_view> text(const std::optional<Id>& id) noexcept {
    return id ? std::optional<std::string_view>(id->view()) : std::nullopt;
}

// A parsed header's keys, views of it, for the import until it takes keys.
[[nodiscard]] inline KeysView keys_of(const header::Header<Chars>& h) {
    return KeysView{h.value(Slot::id),    h.value(Slot::scope),  h.value(Slot::lane),
                    h.value(Slot::kind),  h.seq(),               h.text(Slot::parent),
                    h.text(Slot::target), h.text(Slot::content).has_value()};
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

}  // namespace detail

// Each line's keys from the admit hook, before any lock, the hook's given,
// read and kept at their places, the store's bounds and kinds over the
// keys; a refusal thrown.
template <platform::Platform P, Scratch S, class A>
Checked read(P& platform, std::span<const EntryView> lines, const AtView& at, const Chars& allocator, S& scratch,
             A& admit) {
    if (at.op != Operation::append && at.op != Operation::forget && at.op != Operation::import_) {
        memory::fail_fast("stage::check: an operation that takes no entries");
    }
    if (lines.size() > max_call_entries) {
        too_large(lines.size());
    }
    admit.given(at, lines, allocator, scratch);
    Checked out(allocator);
    out.kept.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        KeysView keys;
        admit.read(platform, at, i, lines[i], keys, allocator, scratch);
        out.size = detail::sum(out.size, detail::sum(lines[i].line.size(), 1));
        if (const std::optional<std::string_view> body = lines[i].body) {
            out.size = detail::sum(out.size, detail::sum(body->size(), 1));
        }
        out.kept.push_back(detail::keep(keys, lines[i], i, at));
        admit.kept(at, i, out.kept.back(), allocator, scratch);
    }
    return out;
}

// What the call stages, and at append the maximum: the store's, after the
// admit hook; a refusal thrown.
template <View V>
Staged staged(V& view, const Checked& checked, const Limits& limits, const AtView& at) {
    const std::span<const KeptView> entries(checked.kept);
    Staged out;
    out.size = checked.size;
    out.count = entries.size();
    if (!entries.empty()) {
        out.head = Head{entries.back().id, entries.back().seq};
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
inline void in_order(std::string_view scope, std::string_view lane, std::optional<Head> now,
                     std::span<const KeptView> entries, const AtView& at) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const KeptView& k = entries[i];
        if (k.scope.view() != scope || k.lane.view() != lane) {
            refuse(at, i, k.id.view(), "parent");
        }
        detail::next(k.id.view(), detail::text(k.parent), k.seq, now, i, at);
        now = Head{k.id, k.seq};
    }
}

// At import, one line as it arrives, before its insert: its parent the
// view's head of its lane and its seq one past it; a refusal thrown.
template <class V>
void arrives(V& view, const header::Header<Chars>& h, std::size_t index, const AtView& at) {
    detail::next(h, view.head(h.value(Slot::scope), h.value(Slot::lane)), index, at);
}

// At forget, each target erased: erased the targets whose body the store
// drops, in the call's order, each once, each held in the call's scope with
// its body; the first entry whose target is not there refused target.
inline void erasable(std::span<const KeptView> entries, std::span<const Id> erased, const AtView& at) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const KeptView& k = entries[i];
        if (i >= erased.size() || erased[i].view() != detail::text(k.target).value_or(std::string_view())) {
            refuse(at, i, k.id.view(), "target");
        }
    }
}

// At import, one entry once every line is in, after the hook's settled,
// by what the store holds of it: its target held, else target; its body
// held while forgotten, forgotten_body; a content with no body, not
// forgotten, body_missing.
inline void settles(const header::Header<Chars>& h, bool target_held, bool forgotten, bool body, std::size_t index,
                    const AtView& at) {
    const std::string_view id = h.value(Slot::id);
    if (h.text(Slot::target) && !target_held) {
        refuse(at, index, id, "target");
    }
    if (body && forgotten) {
        refuse(at, index, id, "forgotten_body");
    }
    if (!body && !forgotten && h.text(Slot::content)) {
        refuse(at, index, id, "body_missing");
    }
}

// At append, each entry's target held in the call's scope, an earlier one
// of the call's or the store's (the view's held_in, by its keys); the
// first not held refused target.
template <class V>
void targets(V& view, std::span<const KeptView> entries, std::string_view scope, const AtView& at) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::optional<Id>& t = entries[i].target;
        bool held = !t;
        for (std::size_t j = 0; j < i && !held; ++j) {
            held = entries[j].id == *t;
        }
        if (!held) {
            held = view.held_in(t->view(), scope);
        }
        if (!held) {
            refuse(at, i, entries[i].id.view(), "target");
        }
    }
}

// The store's step after the admit hook: its own order, then staged.
template <View V>
Staged staged(V& view, const Checked& checked, const Limits& limits, const AtView& at, std::string_view scope,
              std::string_view lane, const std::optional<Head>& now) {
    in_order(scope, lane, now, std::span<const KeptView>(checked.kept), at);
    return staged(view, checked, limits, at);
}

// The same, the store's own guards: its order, at append its targets, then
// staged.
template <View V>
Staged guarded(V& view, const Checked& checked, const Limits& limits, const AtView& at, std::string_view scope,
               std::string_view lane, const std::optional<Head>& now) {
    in_order(scope, lane, now, std::span<const KeptView>(checked.kept), at);
    if (at.op == Operation::append) {
        targets(view, std::span<const KeptView>(checked.kept), scope, at);
    }
    return staged(view, checked, limits, at);
}

}  // namespace qiven3::store::stage
