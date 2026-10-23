#pragma once

// memory: the in-memory reference store. MemoryStore<P> is an object over
// one shared state (memory_state.hpp), which its maker owns and which
// outlives every object made over it; the factory makes a second object
// over the same state, and a read-only one, each by the constructor. Each
// object keeps the thread that made it (Gate); a read-only object serves
// reads alone. It meets the contract within one process by the simplest
// means: under the state's lock, the head compared, then stage's part 2
// against the state's view, then the new rows staged on the state's heap
// and applied by moves that cannot fail, so a refusal leaves the state as
// it was; a forget so too, its guard and whole records checked. Reads run
// under the lock, each header parsed again from its line and its id
// recomputed, each body record parsed and checked against its header, a
// mismatch StoreCorrupt. A scan fixes, at its start, each lane's entries
// then held, and reads one header at a time. Export, import and verify are
// memory_exchange.hpp's. It is not durable, has no files and admits no
// other process: no capability. Its purge is done at once; it has no path,
// so an error names none.
//
// Budgets: each operation runs on its own pages, an arena over them, 1 MiB
// plus 4 times the lines given: the object's own 1 MiB for an operation
// given none, else pages the call takes. At append and forget the pages
// split in two halves, the arena check keeps its entries on, and check's
// scratch, afresh for each line, then admit's clearing's, afresh for each
// direction root. Import and verify run on the object's own;
// an export's lines go to the caller's arena. Each operation's peak is its
// arenas' high-water marks summed, peak() the last one's; the state's heap
// is the store's, as SQLite's files are.
//
// Allocations, by operation (the counts are the cases'): append check's
// and admit's (stage's) on the operation's arenas; on the state's heap,
// the call's rows, three each, a new lane's two and its ids', the room
// each container grows to. head, forgotten, size: none. heads: the caller's
// rows. header: read_header's (canon's four and one, header's check's)
// on the operation's arena, one on the caller's. body: read_header's, then
// body's parse (canon's four and one) on the operation's arena, one on the
// caller's. scan: its lanes' room on the state's heap; each next as
// header. forget: as append, and the targets' room and forgetting's on the
// kept arena. export, import, verify: memory_exchange.hpp's. close and
// purge: none. Platform calls: pages, once at the object's making, once
// per scan and once per call given lines; part 1's and body's wipes, each
// noexcept.
//
// thread: one, each object; the state, shared (its lock).

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/memory_exchange.hpp"
#include "store/memory_state.hpp"
#include "store/stage.hpp"

namespace qiven3::store {

// A scan: the lanes of its range as they stood at its start, each read
// from the seq after its start, one header at a time.
template <platform::Platform P>
class MemoryScan {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));

    MemoryScan(state::State& s, P& platform, const RangeView& range, state::Countdown* countdown)
        : state_(&s), pages_(platform.pages(operation_base)), gate_(false), countdown_(countdown),
          after_(range.after.value_or(0) > 0 ? range.after.value_or(0) : 0) {
        const state::Locked lock(s, Operation::scan, {});
        const state::Data& d = lock.data();
        lanes_.emplace(state::Allocator<Pos>(lock.data().heap));
        for (std::size_t l = 0; l < d.lanes.size(); ++l) {
            const state::Lane& lane = d.lanes[l];
            if ((!range.scope || *range.scope == std::string_view(lane.scope)) &&
                (!range.lane || *range.lane == std::string_view(lane.name))) {
                lanes_->push_back(Pos{static_cast<std::uint32_t>(l), static_cast<std::uint32_t>(lane.ids.size())});
            }
        }
        std::sort(lanes_->begin(), lanes_->end(), [&d](const Pos& a, const Pos& b) {
            const state::Lane& x = d.lanes[a.lane];
            const state::Lane& y = d.lanes[b.lane];
            return x.scope != y.scope ? x.scope < y.scope : x.name < y.name;
        });
        at_ = start();
    }
    MemoryScan(const MemoryScan&) = delete;
    MemoryScan& operator=(const MemoryScan&) = delete;
    MemoryScan(MemoryScan&&) noexcept = default;
    MemoryScan& operator=(MemoryScan&&) noexcept = default;
    ~MemoryScan() = default;

    // The next header into out; false at the end.
    bool next(Header& out) {
        gate_.enter(Operation::scan, {});
        memory::Arena arena(pages_.bytes().first(operation_base));
#if defined(QIVEN3_FAULTS)
        if (countdown_ != nullptr) {
            arena.attach(countdown_);
        }
#endif
        const state::Locked lock(*state_, Operation::scan, {});
        const state::Data& d = lock.data();
        while (lane_ < lanes_->size()) {
            const Pos& p = (*lanes_)[lane_];
            if (at_ < p.end) {
                const state::Held& e = d.held[d.lanes[p.lane].ids[at_]];
                const Header h = state::read_header(e, Operation::scan, {}, state::Chars(arena));
                state::copy_header(h, out);
                peak_ = std::max(peak_, arena.peak());
                ++at_;
                return true;
            }
            ++lane_;
            at_ = start();
        }
        return false;
    }
    // The counted peak of its greatest next.
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }

private:
    struct Pos {
        std::uint32_t lane = 0;
        std::uint32_t end = 0;  // the lane's entries at the scan's start
    };
    // The first position past after: seq n is at n - 1.
    [[nodiscard]] std::uint32_t start() const noexcept {
        return after_ > std::int64_t{state::none} ? state::none : static_cast<std::uint32_t>(after_);
    }

    state::State* state_;
    Pages pages_;
    std::optional<state::Rows<Pos>> lanes_;
    Gate gate_;
    [[maybe_unused]] state::Countdown* countdown_;
    std::int64_t after_;
    std::size_t lane_ = 0;
    std::uint32_t at_ = 0;
    std::size_t peak_ = 0;
};

template <platform::Platform P>
class MemoryStore {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));
    static constexpr Capabilities capabilities{};

    // An object over the state on the platform, both outliving it; a
    // read-only one serves reads alone.
    MemoryStore(state::State& s, P& platform, bool read_only = false)
        : state_(&s), platform_(&platform), pages_(platform.pages(operation_base)), gate_(read_only) {
        if (pages_.bytes().size() < operation_base) {
            memory::fail_fast("store/memory: pages short of their request");
        }
    }
    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;
    // A moved-from object is closed.
    MemoryStore(MemoryStore&& other) noexcept
        : state_(other.state_), platform_(other.platform_), pages_(std::move(other.pages_)), gate_(other.gate_),
          countdown_(other.countdown_), owner_(other.owner_) {
        other.gate_.close();
    }
    MemoryStore& operator=(MemoryStore&& other) noexcept {
        if (this != &other) {
            state_ = other.state_;
            platform_ = other.platform_;
            pages_ = std::move(other.pages_);
            gate_ = other.gate_;
            countdown_ = other.countdown_;
            owner_ = other.owner_;
            other.gate_.close();
        }
        return *this;
    }
    // Closes without the gate: a memory store has nothing to give back.
    ~MemoryStore() = default;

    Written append(std::string_view scope, std::string_view lane, Expected expected,
                   std::span<const EntryView> entries) {
        constexpr Operation op = Operation::append;
        gate_.enter(op, {});
        std::optional<Pages> extra;
        const std::span<std::byte> span = room(state::budget_of(entries), extra);
        const std::size_t half = span.size() / 2;
        memory::Arena kept(span.first(span.size() - half));
        hook(kept);
        state::ScratchView scratch(span.subspan(span.size() - half), countdown_);
        const state::Chars chars(kept);
        const stage::AtView at{op, {}};
        const stage::Checked<P> checked = stage::check(*platform_, entries, at, chars, scratch);
        const state::Locked lock(*state_, op, {});
        state::Data& d = lock.data();
        const std::optional<Head> now = state::expect(d, op, {}, scope, lane, expected);
        state::StateView view(d, chars, op, {}, &scratch);
        const stage::Staged staged = stage::admit(view, scope, lane, expected, checked, state_->limits(), at, chars);
        state::Batch batch(d);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            batch.add(d, entries[i].line, entries[i].body, checked.entries[i].header());
        }
        batch.apply(d, staged.size);
        peak_ = kept.peak() + scratch.peak();
        return Written{entries.empty() ? now.value_or(Head{}) : staged.head,
                       measure(d.size, d.count, state_->limits()), Purge{}};
    }

    std::optional<Head> head(std::string_view scope, std::string_view lane) {
        gate_.enter(Operation::head, {});
        const state::Locked lock(*state_, Operation::head, {});
        const state::Data& d = lock.data();
        const std::uint32_t l = state::lane_of(d, scope, lane);
        return l == state::none ? std::nullopt : std::optional<Head>(d.lanes[l].head);
    }

    Heads heads(Scopes scopes, Vector<LaneHead>& rows) {
        gate_.enter(Operation::heads, {});
        const state::Locked lock(*state_, Operation::heads, {});
        state::heads_of(lock.data(), scopes, rows);
        return Heads{state::forgets_in(lock.data(), scopes), Purge{}};
    }

    void header(std::string_view id, Header& out) {
        constexpr Operation op = Operation::header;
        gate_.enter(op, {});
        memory::Arena arena(own());
        hook(arena);
        const state::Locked lock(*state_, op, {});
        const state::Held& e = held(lock.data(), op, id);
        const Header h = state::read_header(e, op, {}, state::Chars(arena));
        state::copy_header(h, out);
        peak_ = arena.peak();
    }

    // false: forgotten, a forget, or a header with no body.
    bool body(std::string_view id, String& record) {
        constexpr Operation op = Operation::body;
        gate_.enter(op, {});
        memory::Arena arena(own());
        hook(arena);
        const state::Chars chars(arena);
        const state::Locked lock(*state_, op, {});
        const state::Held& e = held(lock.data(), op, id);
        if (!e.has_body) {
            peak_ = arena.peak();
            return false;
        }
        const Header h = state::read_header(e, op, {}, chars);
        state::check_body(*platform_, e, h, op, chars);
        record.assign(e.body);
        peak_ = arena.peak();
        return true;
    }

    bool forgotten(std::string_view id) {
        constexpr Operation op = Operation::forgotten;
        gate_.enter(op, {});
        const state::Locked lock(*state_, op, {});
        return held(lock.data(), op, id).forget != state::none;
    }

    MemoryScan<P> scan(const RangeView& range) {
        gate_.enter(Operation::scan, {});
        return MemoryScan<P>(*state_, *platform_, range, countdown_);
    }

    // As append, but forget entries alone, every lane as the guard expects
    // it and whole records; the targets' bodies dropped, their ids into
    // erased; never refused for size or count.
    Written forget(std::string_view scope, std::string_view lane, Expected expected,
                   std::span<const EntryView> entries, std::span<const GuardView> guard, Vector<Id>& erased) {
        constexpr Operation op = Operation::forget;
        gate_.enter(op, {});
        std::optional<Pages> extra;
        const std::span<std::byte> span = room(state::budget_of(entries), extra);
        const std::size_t half = span.size() / 2;
        memory::Arena kept(span.first(span.size() - half));
        hook(kept);
        state::ScratchView scratch(span.subspan(span.size() - half), countdown_);
        const state::Chars chars(kept);
        const stage::AtView at{op, {}};
        const stage::Checked<P> checked = stage::check(*platform_, entries, at, chars, scratch);
        const state::Locked lock(*state_, op, {});
        state::Data& d = lock.data();
        const std::optional<Head> now = state::expect(d, op, {}, scope, lane, expected);
        state::guard(d, guard);
        state::StateView view(d, chars, op, {}, &scratch);
        const stage::Staged staged = stage::admit(view, scope, lane, expected, checked, state_->limits(), at, chars);
        Vector<std::uint32_t> targets(chars);
        targets.reserve(entries.size());
        for (const stage::Entry<P>& e : checked.entries) {
            targets.push_back(state::find(d, e.header().text(state::header::Slot::target).value_or("")));
        }
        const std::uint64_t dropped = state::forgetting(d, targets, erased, chars);
        state::Batch batch(d);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            batch.add(d, entries[i].line, entries[i].body, checked.entries[i].header());
        }
        batch.apply(d, staged.size);
        d.size -= dropped;
        peak_ = kept.peak() + scratch.peak();
        return Written{entries.empty() ? now.value_or(Head{}) : staged.head,
                       measure(d.size, d.count, state_->limits()), Purge{}};
    }

    // Done at once: a forget drops its bodies as it commits.
    Purge purge() {
        gate_.enter(Operation::purge, {});
        return Purge{};
    }

    // A snapshot of the scope set, read one line at a time.
    state::MemoryExport export_(Scopes scopes, std::span<const AfterView> after) {
        gate_.enter(Operation::export_, {});
        const state::Locked lock(*state_, Operation::export_, {});
        return state::MemoryExport(*state_, lock.data(), owner_, scopes, after);
    }

    // Into an empty state, all or nothing; the heads it wrote into heads.
    template <Source R>
    Imported import_(R& source, Vector<LaneHead>& heads) {
        constexpr Operation op = Operation::import_;
        gate_.enter(op, {});
        if constexpr (std::same_as<R, state::MemoryExport>) {
            // An export of this state: the state was empty at its making,
            // or is not now; its next would wait on this call's lock.
            if (source.over(*state_)) {
                const state::Locked lock(*state_, op, {});
                if (lock.data().count != 0) {
                    throw StoreError::not_empty(name(op), {}, lock.data().count);
                }
                heads.clear();
                return Imported{Counts{}, measure(0, 0, state_->limits())};
            }
        }
        state::Peak peak;
        const Imported in = state::import_source(*platform_, *state_, source, heads, own(), countdown_, peak);
        peak_ = peak.sum();
        return in;
    }

    Sizes size() {
        gate_.enter(Operation::size, {});
        const state::Locked lock(*state_, Operation::size, {});
        Sizes s;
        s.volume = measure(lock.data().size, lock.data().count, state_->limits());
        return s;
    }

    Counts verify() {
        gate_.enter(Operation::verify, {});
        const state::Locked lock(*state_, Operation::verify, {});
        state::Peak peak;
        const Counts counts = state::verify_state(*platform_, lock.data(), own(), countdown_, peak);
        peak_ = peak.sum();
        return counts;
    }

    // Its exports closed with it.
    Purge close() {
        gate_.enter(Operation::close, {});
        if (owner_ != 0) {
            const state::Locked lock(*state_, Operation::close, {});
            state::close_exports(lock.data(), owner_);
        }
        gate_.close();
        return Purge{};
    }

    [[nodiscard]] bool read_only() const noexcept { return gate_.read_only(); }
    [[nodiscard]] Limits limits() const noexcept { return state_->limits(); }
    // The counted peak of the last append, forget, header, body, import or
    // verify that returned: its arenas' high-water marks summed.
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }

#if defined(QIVEN3_FAULTS)
    // Each operation's arenas ask the countdown first; null detaches it.
    void attach(state::Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    // The object's own pages, an operation's given no lines.
    [[nodiscard]] std::span<std::byte> own() const noexcept { return pages_.bytes().first(operation_base); }
    // An operation's pages: the object's own where they hold its budget,
    // else the call's own, taken from the platform.
    std::span<std::byte> room(std::size_t budget, std::optional<Pages>& extra) {
        if (budget <= operation_base) {
            return own().first(budget);
        }
        extra.emplace(platform_->pages(budget));
        if (extra->bytes().size() < budget) {
            memory::fail_fast("store/memory: pages short of their request");
        }
        return extra->bytes().first(budget);
    }
    void hook([[maybe_unused]] memory::Arena& arena) const noexcept {
#if defined(QIVEN3_FAULTS)
        if (countdown_ != nullptr) {
            arena.attach(countdown_);
        }
#endif
    }
    // The entry of an id: NotFound when the state holds none.
    static const state::Held& held(const state::Data& d, Operation op, std::string_view id) {
        const std::uint32_t i = state::find(d, id);
        if (i == state::none) {
            throw StoreError::not_found(name(op), {}, id);
        }
        return d.held[i];
    }

    state::State* state_;
    P* platform_;
    Pages pages_;
    Gate gate_;
    state::Countdown* countdown_ = nullptr;
    std::uint64_t owner_ = 0;  // the object's number in the state, given at its first export
    std::size_t peak_ = 0;
};

}  // namespace qiven3::store
