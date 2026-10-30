#pragma once

// memory_exchange: the in-memory store's export, import and verify, and
// the pieces its forget and its other operations share (memory.hpp).
//
// Forget: under the lock, after the head is compared, guard compares every
// lane the state holds with the guard, a lane moved, absent from the guard
// or unknown to the state a HeadConflict naming it; after the admit hook,
// forgetting writes the ids whose body record the forget drops, giving
// those records' bytes.
//
// Export: under the lock, the snapshot: the lanes of its scope set, in
// (scope, lane) order, each with its length then and its first position
// after its seq, the set's forget count, the size, the count and the
// entries held; it is registered in the state, under its store object's
// number. Each next copies one entry's line, body record and keys as held
// into the caller's Line, under the lock: an entry is forgotten in the snapshot when
// its first forget was held then; its body is yielded unless so, a body a
// later forget dropped staying in place while any export is open.
// It ends at its close, at its store object's close, or at its end.
//
// Import: into an empty state alone (StoreNotEmpty), under the lock for the
// whole call; the source's heads first, then each line's keys from the
// admit hook's read, the store's bounds and chain over them as it arrives,
// and in at once by its keys, no body dropped; once every line is in, the
// ids sorted (no find answers before it), the first id repeated refused
// id_held, the targets and forgets linked by the target keys, then each
// entry, its keys as held, through the hook's settled and the store's
// settles (forgotten_body and body_missing among them) and the heads
// compared. A refusal empties the state again: it was empty, so all or
// nothing.
//
// Verify: under the lock, each lane's entries in seq order by their keys,
// no line or record read: its seq and lane its place's; its target and
// forget links each other's, a forget's target held and forgotten first
// by it; no body for a forgotten id or a forget (forgotten_body); a body
// with no body key, or a body key with no body and no forget, incoherent.
// Then each head, the ids' order and the counters, which give its counts.
//
// Allocations. forget: erased's room, one, on the call's allocator. export: its lanes' room on
// the state's heap, one; the registration's room as it grows; heads, the
// caller's rows' room, one; next, the line's, record's and kind key's room
// on the caller's arena, one each where short. import:
// the source's heads (the source's own); per line its text (the source's)
// and the hook's read's on a fresh arena; the state's as append's
// (make_held's three, a new lane's two, the room each container grows to),
// and the target keys' room as it grows on the state's heap, given back at
// the call's end; per entry the hook's settled's on a fresh arena. verify:
// none. Platform calls: the hooks' own.
//
// thread: one, each object; the state, shared (its lock).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/memory_state.hpp"
#include "store/stage.hpp"

namespace qiven3::store::state {

// An operation's counted peak: the high-water marks of the arenas it holds
// at once, summed; of the fresh arenas over the same pages, the greatest.
struct Peak {
    std::size_t kept = 0;  // the arena kept for the call
    std::size_t line = 0;  // the line in hand's
    std::size_t work = 0;  // each line's or entry's work
    [[nodiscard]] std::size_t sum() const noexcept { return kept + line + work; }
};
inline void note(std::size_t& mark, const memory::Arena& arena) noexcept { mark = std::max(mark, arena.peak()); }

// An operation's budget for the lines it is given.
inline std::size_t budget_of(std::span<const EntryView> entries) {
    const std::optional<std::size_t> lines = given(entries);
    const std::optional<std::size_t> b = lines ? operation_budget(*lines) : std::nullopt;
    if (!b) {
        throw memory::MemoryError::budget("store/memory", std::numeric_limits<std::size_t>::max(), entries.size(), 0);
    }
    return *b;
}

// forget's guard: every lane held as the guard expects it.
void guard(const Data& d, std::span<const GuardView> guard);
// forget's whole_record, then the ids whose body the call drops into
// erased (each held in scope with its body, once); the dropped records'
// bytes, each with its LF. targets: each entry's target's index, in the
// call's order.
std::uint64_t forgetting(const Data& d, std::span<const std::uint32_t> targets, std::string_view scope,
                         Vector<Id>& erased);
// The exports of the store object numbered owner closed; at the last one,
// the bodies forgets dropped while they were open freed.
void close_exports(Data& d, std::uint64_t owner) noexcept;

// An imported entry's target key, by the entry's index, until linked.
struct Link {
    std::uint32_t at = 0;
    Id target;
};
// import's pieces: one line in by its keys, its lane made where new, its
// target and forget none, its target key into links; the ids sorted; an
// entry's target and forget linked; the state emptied again.
void import_one(Data& d, const EntryView& line, Rows<Link>& links);
void sort_ids(Data& d) noexcept;
// The ids sorted, the first entry in the store's order with an earlier one's
// id, or none; the targets linked, what settles asks of entry i.
[[nodiscard]] std::uint32_t repeated(const Data& d) noexcept;
[[nodiscard]] stage::Standing standing(const Data& d, std::uint32_t i) noexcept;
void link(Data& d, std::uint32_t i, std::string_view target) noexcept;
void reset(Data& d) noexcept;
// Entry i's keys as the state holds them, views of it: its parent the one
// before it on its lane, its target the one given.
[[nodiscard]] KeysView held_keys(const Data& d, std::uint32_t i, std::optional<std::string_view> target) noexcept;
// verify's pieces: one entry, the k-th of lane l, by its keys; then the
// heads, the ids' order and the counters, giving the counts.
void verify_entry(const Data& d, std::uint32_t l, std::size_t k);
[[nodiscard]] Counts verify_totals(const Data& d);

// The export's handle: a move-only value, ended by its close, its store
// object's close or its destructor.
class MemoryExport {
public:
    // The snapshot of d, under the lock held over s; owner is the store
    // object's number, given at its first export.
    MemoryExport(State& s, Data& d, std::uint64_t& owner, const Scopes& scopes, std::span<const AfterView> after);
    MemoryExport(const MemoryExport&) = delete;
    MemoryExport& operator=(const MemoryExport&) = delete;
    MemoryExport(MemoryExport&& other) noexcept;
    MemoryExport& operator=(MemoryExport&& other) noexcept;
    ~MemoryExport() { release(); }

    // The snapshot's heads into rows, its forget count and its volume.
    Snapshot heads(Vector<LaneHead>& rows);
    // The next line, false at the end.
    bool next(Line& line);
    // Ends the export; again, nothing. From another thread than its
    // maker's, StoreError thread, the export left open.
    void close() {
        gate_.own(Operation::close, {});
        release();
    }
    [[nodiscard]] bool over(const State& s) const noexcept { return state_ == &s; }

private:
    struct Pos {
        std::uint32_t lane = 0;
        std::uint32_t start = 0;  // the first position after its seq
        std::uint32_t end = 0;    // the lane's entries in the snapshot
    };
    // The export still registered in d: else StoreError closed.
    void open(const Data& d) const;
    void release() noexcept;

    State* state_;
    Rows<Pos> lanes_;
    Gate gate_;
    Snapshot snapshot_;
    std::uint64_t id_ = 0;
    std::uint32_t held_ = 0;  // the entries held at the snapshot
    std::size_t lane_ = 0;
    std::uint32_t at_ = 0;
};

// import, on pages of the object's own: a quarter for the source's heads,
// a quarter for the line in hand, the half left for each line's work.
template <platform::Platform P, Source R, class A>
Imported import_source(P& platform, State& s, R& source, Vector<LaneHead>& heads, std::span<std::byte> pages,
                       Countdown* countdown, Peak& peak, A& admit) {
    constexpr Operation op = Operation::import_;
    const stage::AtView at{op, {}};
    const std::size_t quarter = pages.size() / 4;
    memory::Arena kept(pages.first(quarter));
    hook(kept, countdown);
    const Chars keep(kept);
    Vector<LaneHead> given(keep);
    source.heads(given);
    const Locked lock(s, op, {});
    Data& d = lock.data();
    if (d.count != 0) {
        throw StoreError::not_empty(name(op), {}, d.count);
    }
    Counts counts;
    Rows<Link> links{Allocator<Link>(d.heap)};
    try {
        for (std::size_t i = 0;; ++i) {
            memory::Arena text(pages.subspan(quarter, quarter));
            hook(text, countdown);
            Line line(text);
            const bool more = source.next(line);
            note(peak.line, text);
            if (!more) {
                break;
            }
            memory::Arena work(pages.subspan(2 * quarter));
            hook(work, countdown);
            const Released<A> done(admit);
            const EntryView in{line.header, line.has_body ? std::optional<std::string_view>(line.body) : std::nullopt,
                               line.keys.view()};
            KeysView keys;
            admit.read(platform, at, i, in, keys, Chars(work));
            const EntryView v{in.line, in.body, keys};
            StateView view(d, Chars(work), op, {});
            stage::arrives(view, keys, v, i, at);
            admit.arrived(view, v, i, at);
            import_one(d, v, links);
            note(peak.work, work);
        }
        sort_ids(d);
        if (const std::uint32_t r = repeated(d); r != none) {
            stage::refuse(at, r, d.held[r].id.view(), "id_held");
        }
        for (const Link& l : links) {
            link(d, l.at, l.target.view());
        }
        std::size_t linked = 0;
        for (std::uint32_t i = 0; i < d.held.size(); ++i) {
            memory::Arena work(pages.subspan(2 * quarter));
            hook(work, countdown);
            const bool targets = linked < links.size() && links[linked].at == i;
            const KeysView keys = held_keys(
                d, i, targets ? std::optional<std::string_view>(links[linked++].target.view()) : std::nullopt);
            const Held& e = d.held[i];
            const EntryView v{std::string_view(e.line),
                              e.has_body ? std::optional<std::string_view>(e.body) : std::nullopt, keys};
            StateView view(d, Chars(work), op, {});
            admit.settled(view, v, i, at, Chars(work));
            stage::settles(keys, standing(d, i), i, at);
            note(peak.work, work);
        }
        heads_of(d, std::nullopt, heads);
        admit.heads(std::span<const LaneHead>(given), std::span<const LaneHead>(heads), at);
        counts = verify_totals(d);
        note(peak.kept, kept);
    } catch (...) {
        reset(d);
        heads.clear();
        throw;
    }
    return Imported{counts, measure(d.size, d.count, s.limits())};
}

// verify: each lane's entries in seq order by their keys, then the totals.
inline Counts verify_state(const Data& d) {
    for (std::uint32_t l = 0; l < d.lanes.size(); ++l) {
        for (std::size_t k = 0; k < d.lanes[l].ids.size(); ++k) {
            verify_entry(d, l, k);
        }
    }
    return verify_totals(d);
}

}  // namespace qiven3::store::state
