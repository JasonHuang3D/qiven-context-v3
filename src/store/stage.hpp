#pragma once

// stage: the store's steps of an append, a forget or an import: each
// entry's keys from the admit hook, kept; the store's own guards and order
// over them; the maximum. The rules of cognition are an admit hook's (the
// contract's Admit and AdmitImport), the product's; at append and forget
// the store runs none of them and reads no line. Its own guards run
// whatever hook it is given, so that it never commits a state its verify
// or open refuses. At import too the store reads no line: it takes each
// line's keys from the hook's read. With NoAdmit the store runs no rule of
// cognition's: it reads no line and no body, its guards over the keys
// alone.
//
// read(platform, lines, at, allocator, scratch, admit), before any lock,
// entry by entry: the hook's given before the first line; then each line's
// keys from the hook's read; the store's bounds over them: id, parent and
// target at most Id's capacity, scope, lane and kind at most their bytes,
// the entry's line, body and keys at most entry_bytes (field entry), else
// EntryInvalid too_long, the key's name as its field (a seq below 1 is
// next's: seq); a body key with no record body_missing, a record with no
// body key body_mismatch; a forget with no target key target, with a body
// key content (part 1's codes); at forget a kind but forget StoreError
// forget_kind, its detail naming the entry, at append a forget
// EntryInvalid forget_path (header.kind); the keys kept, then the hook's
// kept. It returns the kept entries and the bytes the lines add: each
// header line and body record with its LF, and each kind.
//
// arrives(view, keys, line, index, at), at import as each line arrives,
// after the hook's read and before its arrived: the keys' bounds as read's
// and a record with no body key body_mismatch, a forget's target and body
// keys as read's (a body key with no record waits for settles: the body
// may be forgotten), then its parent the
// view's head of its lane and its seq one past it, refused parent or seq.
// erasable(entries, erased, at), at
// forget once the store has found what it erases: each forget's target
// held in the call's scope with its body, none twice, the first not
// refused EntryInvalid target. settles(keys, ...), at import once every
// line is in, after the hook's settled: a target key not held, target; a
// forget whose target has no body key, is another scope's, is a forget or
// has an earlier forget, target; a body of a forgotten entry,
// forgotten_body; a body key with no body, not forgotten, body_missing.
//
// staged(view, checked, limits, at), inside the store's transaction or
// lock, after the hook's admit: at append alone, the maximum: the view's
// size and count with the call's added past the limits' maximum is
// StoreFull store_full, with the size and count the call would reach. It
// returns the new head and what the call adds. With the call's scope, lane
// and the head the store read, staged first runs in_order, the store's own
// order over its keys; guarded the same, fresh, at append then targets, each
// entry's target held as settles reads it at import (the view's held, any
// scope): the store's, the call's earlier one or itself; else target; its
// scope, or itself as its target, the hook's to refuse. A HeadConflict
// and a guard are the store's. fresh: an id held, or an earlier one of the
// call's, refused EntryInvalid id_held, after the order, so that an entry
// written again keeps its HeadConflict or order refusal, and before any
// write. At import SQLite refuses it as the line arrives, after arrives;
// the memory store once its ids sort, every line in.
//
// Refusals are EntryInvalid with the entry's index and its id key (none
// where the id is past its bound).
//
// Allocations. read: on the caller's allocator, the kept entries' room,
// one; the hook's are its own (each hook says). staged, in_order, arrives,
// erasable, settles, fresh and targets none but the view's. Platform
// calls: none but the hook's (each hook says). stage logs nothing.
//
// thread: none; the kept entries, one.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "keys/keys.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::stage {

using Chars = memory::Allocator<char>;

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

// The view staged reads: the size and count held.
template <class V>
concept View = requires(const V& v) {
    { v.size() } -> std::same_as<std::uint64_t>;
    { v.count() } -> std::same_as<std::uint64_t>;
};

// The scratch: each call a fresh arena over the same pages, the previous
// one's blocks all given back.
template <class S>
concept Scratch = requires(S& s) {
    { s() } -> std::same_as<memory::Arena&>;
};

// What the store holds of an import's entry once every line is in, for
// settles; the target's four read for a forget with its target held.
struct Standing {
    bool target_held = false;    // no target key, or its entry is held
    bool target_body = false;    // the target has a body key
    bool target_scope = false;   // the target is of the entry's scope
    bool target_forget = false;  // the target is a forget
    bool forget_before = false;  // a forget of the same target is earlier in the store's order
    bool forgotten = false;      // a forget targets this entry
    bool body = false;           // this entry's body is held
};

// The refusals, each written by stage.cpp.
[[noreturn]] void refuse(const AtView& at, std::size_t index, std::string_view id, std::string_view code,
                         std::string_view field = {}, std::string_view pattern = {});
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
// The store's bounds over one entry's keys, and a record with no body key
// refused body_mismatch, then a forget with no target key target, with a
// body key content; the id key, or none where it is past its bound.
inline std::string_view bounds(const KeysView& k, const EntryView& line, std::size_t index, const AtView& at) {
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
    bound(k.kind.size() <= kind_bytes, "kind");
    std::uint64_t bytes = sum(line.line.size(), line.body ? line.body->size() : 0);
    for (const std::string_view key : {k.id, k.scope, k.lane, k.kind, k.parent.value_or(""), k.target.value_or("")}) {
        bytes += key.size();
    }
    bound(bytes <= entry_bytes, "entry");
    if (!k.body && line.body) {
        refuse(at, index, id, "body_mismatch");
    }
    if (k.kind == "forget" && !k.target) {
        refuse(at, index, id, "target");
    }
    if (k.kind == "forget" && k.body) {
        refuse(at, index, id, "content");
    }
    return id;
}

// The store's bounds over one entry's keys and its record, then its kind
// at its operation; the entry kept.
inline KeptView keep(const KeysView& k, const EntryView& line, std::size_t index, const AtView& at) {
    const std::string_view id = bounds(k, line, index, at);
    if (k.body && !line.body) {
        refuse(at, index, id, "body_missing");
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
        out.size = detail::sum(out.size, detail::sum(lines[i].line.size(), 1 + keys.kind.size()));
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

// At import, one line as it arrives, before its insert, by its keys: their
// bounds, a record with no body key and a forget's target and body keys
// (detail::bounds), then its parent
// the view's head of its lane and its seq one past it; a refusal thrown.
template <class V>
void arrives(V& view, const KeysView& k, const EntryView& line, std::size_t index, const AtView& at) {
    (void)detail::bounds(k, line, index, at);
    detail::next(k.id, k.parent, k.seq, view.head(k.scope, k.lane), index, at);
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
// by its keys and what the store holds of it: a target key not held,
// target; a forget of a target with no body key, of another scope, itself
// a forget or forgotten earlier, target; its body held while forgotten,
// forgotten_body; a body key with no body, not forgotten, body_missing.
inline void settles(const KeysView& k, const Standing& s, std::size_t index, const AtView& at) {
    const std::string_view id = k.id;
    if (k.target && !s.target_held) {
        refuse(at, index, id, "target");
    }
    if (k.kind == "forget" && (!s.target_body || !s.target_scope || s.target_forget || s.forget_before)) {
        refuse(at, index, id, "target");
    }
    if (s.body && s.forgotten) {
        refuse(at, index, id, "forgotten_body");
    }
    if (!s.body && !s.forgotten && k.body) {
        refuse(at, index, id, "body_missing");
    }
}

// At append, each entry's target held, as settles holds it at import: the
// entry itself, an earlier one of the call's or the store's in any scope
// (the view's held, by its keys); the first not held refused target.
template <class V>
void targets(V& view, std::span<const KeptView> entries, const AtView& at) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::optional<Id>& t = entries[i].target;
        bool held = !t;
        for (std::size_t j = 0; j <= i && !held; ++j) {
            held = entries[j].id == *t;
        }
        if (!held) {
            held = view.held(t->view());
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

// At append and forget, each entry's id new: not held (the view's held, in
// any scope), not an earlier one of the call's; the first refused id_held.
template <class V>
void fresh(V& view, std::span<const KeptView> entries, const AtView& at) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        bool held = view.held(entries[i].id.view());
        for (std::size_t j = 0; j < i && !held; ++j) {
            held = entries[j].id == entries[i].id;
        }
        if (held) {
            refuse(at, i, entries[i].id.view(), "id_held");
        }
    }
}

// The same, the store's own guards: its order, each id new, at append its
// targets, then staged.
template <View V>
Staged guarded(V& view, const Checked& checked, const Limits& limits, const AtView& at, std::string_view scope,
               std::string_view lane, const std::optional<Head>& now) {
    in_order(scope, lane, now, std::span<const KeptView>(checked.kept), at);
    fresh(view, std::span<const KeptView>(checked.kept), at);
    if (at.op == Operation::append) {
        targets(view, std::span<const KeptView>(checked.kept), at);
    }
    return staged(view, checked, limits, at);
}

}  // namespace qiven3::store::stage
