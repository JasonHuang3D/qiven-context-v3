#pragma once

// memory_exchange: the in-memory store's export, import and verify, and
// the pieces its forget and its other operations share (memory.hpp).
//
// Forget: under the lock, after the head is compared, guard compares every
// lane the state holds with the guard, a lane moved, absent from the guard
// or unknown to the state a HeadConflict naming it; after part 2,
// forgetting refuses StoreError whole_record when a revision of a target
// (an entry of a kind but the change kinds naming it) is neither forgotten
// nor itself a target of the call, and writes the ids whose body record
// the forget drops, giving those records' bytes.
//
// Export: under the lock, the snapshot: the lanes of its scope set, in
// (scope, lane) order, each with its length then and its first position
// after its seq, the set's forget count, the size, the count and the
// entries held; it is registered in the state, under its store object's
// number. Each next copies one entry's line and body record into the
// caller's Line, under the lock: an entry is forgotten in the snapshot when
// its first forget was held then; its body is yielded unless so, a body a
// later forget dropped staying in place while any export is open.
// It ends at its close, at its store object's close, or at its end.
//
// Import: into an empty state alone (StoreNotEmpty), under the lock for the
// whole call; the source's heads first, then each line through part 1 and
// the chain rules as it arrives and in at once, no body dropped; once every
// line is in, the ids sorted, the targets and forgets linked, then each
// entry through the final view's rules (forgotten_body and body_missing
// among them) and the heads compared. A refusal empties the state again:
// it was empty, so all or nothing.
//
// Verify: under the lock, each lane's entries in seq order: the header
// parsed and its id recomputed, its body checked, both as the reads do;
// part 1 again, a fault StoreCorrupt incoherent; seq, parent, scope, lane
// and target; no body for a forgotten id (forgotten_body); every entry but
// a forget, with content and not forgotten, with its body. Then each head,
// the ids' order and the counters, which give its counts.
//
// Allocations. forget: forgetting's sorted copy of the targets, one, and
// erased's room, one, on the call's allocator. export: its lanes' room on
// the state's heap, one; the registration's room as it grows; heads, the
// caller's rows' room, one; next, the line's and record's room on the
// caller's arena, one each where short. import:
// the source's heads (the source's own); per line its text (the source's)
// and part 1's on a fresh arena; the state's as append's (make_held's
// three, a new lane's two, the room each container grows to); per entry
// read_header's and the final rules' on a fresh arena. verify: per entry
// read_header's, check_body's and part 1's on a fresh arena. Platform calls:
// part 1's and body's wipes and keys, each noexcept.
//
// thread: one, each object; the state, shared (its lock).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
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

// A held entry's body record, checked against its header: a record the
// parser refuses StoreCorrupt incoherent, one not its header's
// StoreCorrupt body_mismatch.
template <platform::Platform P>
void check_body(P& platform, const Held& e, const Header& h, Operation op, const Chars& allocator) {
    namespace body = entry::body;
    try {
        const body::Body<P, Chars> b = body::parse(platform, std::string_view(e.body), allocator);
        body::check(b, h);
    } catch (const body::BodyError& f) {
        throw StoreError::corrupt(name(op), {}, f.code == body::Code::body_mismatch ? Corrupt::body_mismatch
                                                                                   : Corrupt::incoherent,
                                  e.id.view());
    } catch (const ParseError&) {
        throw StoreError::corrupt(name(op), {}, Corrupt::incoherent, e.id.view());
    }
}

// forget's guard: every lane held as the guard expects it.
void guard(const Data& d, std::span<const GuardView> guard);
// forget's whole_record, then the ids whose body the call drops into
// erased; the dropped records' bytes, each with its LF. targets: each
// entry's target's index, in the call's order.
std::uint64_t forgetting(const Data& d, std::span<const std::uint32_t> targets, Vector<Id>& erased,
                         const Chars& allocator);
// The exports of the store object numbered owner closed; at the last one,
// the bodies forgets dropped while they were open freed.
void close_exports(Data& d, std::uint64_t owner) noexcept;

// import's pieces: one line in, its lane made where new, its target and
// forget none; the ids sorted; an entry's target and forget linked; the
// state emptied again.
void import_one(Data& d, std::string_view line, std::optional<std::string_view> record, const Header& h);
void sort_ids(Data& d) noexcept;
void link(Data& d, std::uint32_t i, const Header& h) noexcept;
void reset(Data& d) noexcept;
// verify's pieces: one entry, the k-th of lane l, against its header; then
// the heads, the ids' order and the counters, giving the counts.
void verify_entry(const Data& d, std::uint32_t l, std::size_t k, const Header& h);
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
            const EntryView v{line.header, line.has_body ? std::optional<std::string_view>(line.body) : std::nullopt};
            const stage::Entry<P> e = stage::detail::part1(platform, at, i, v, Chars(work));
            StateView view(d, Chars(work), op, {});
            admit.arrived(view, e.header(), i, at);
            import_one(d, v.line, v.body, e.header());
            note(peak.work, work);
        }
        sort_ids(d);
        for (std::size_t pass = 0; pass < 2; ++pass) {
            for (std::uint32_t i = 0; i < d.held.size(); ++i) {
                memory::Arena work(pages.subspan(2 * quarter));
                hook(work, countdown);
                const Header h = read_header(d.held[i], op, {}, Chars(work));
                if (pass == 0) {
                    link(d, i, h);
                    note(peak.work, work);
                    continue;
                }
                const Held& e = d.held[i];
                const std::optional<std::string_view> content =
                    e.has_body ? std::optional<std::string_view>(entry::body::detail::parts(e.body).content)
                               : std::nullopt;
                StateView view(d, Chars(work), op, {});
                admit.settled(view, h, content, i, at, Chars(work));
                note(peak.work, work);
            }
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

// verify, each entry's work on a fresh arena over pages.
template <platform::Platform P>
Counts verify_state(P& platform, const Data& d, std::span<std::byte> pages, Countdown* countdown, Peak& peak) {
    constexpr Operation op = Operation::verify;
    for (std::uint32_t l = 0; l < d.lanes.size(); ++l) {
        const Lane& lane = d.lanes[l];
        for (std::size_t k = 0; k < lane.ids.size(); ++k) {
            memory::Arena arena(pages);
            hook(arena, countdown);
            const Chars chars(arena);
            const Held& e = d.held[lane.ids[k]];
            const Header h = read_header(e, op, {}, chars);
            const std::optional<std::string_view> record =
                e.has_body ? std::optional<std::string_view>(e.body) : std::nullopt;
            if (record) {
                check_body(platform, e, h, op, chars);
            }
            try {
                (void)stage::detail::part1(platform, stage::AtView{op, {}}, k, EntryView{e.line, record}, chars);
            } catch (const StoreError& f) {
                if (f.kind() != Kind::entry_invalid) {
                    throw;
                }
                throw StoreError::corrupt(name(op), {}, Corrupt::incoherent, f.entry_code());
            }
            verify_entry(d, l, k, h);
            note(peak.work, arena);
        }
    }
    return verify_totals(d);
}

}  // namespace qiven3::store::state
