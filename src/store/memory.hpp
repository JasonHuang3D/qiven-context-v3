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
// it was. Reads run under the lock, each header parsed again from its line
// and its id recomputed, each body record parsed and checked against its
// header, a mismatch StoreCorrupt. A scan fixes, at its start, each lane's
// entries then held, and reads one header at a time. It is not durable,
// has no files and admits no other process: no capability. Its purge is
// done at once; it has no path, so an error names none.
//
// Budgets: each operation runs on its own pages, an arena over them, 1 MiB
// plus 4 times the lines given: the object's own 1 MiB for an operation
// given none, else pages the call takes. At append the pages split in two
// halves, the arena check keeps its entries on, and check's scratch,
// afresh for each line. Each operation's peak is its arenas' high-water
// marks summed; the state's heap is the store's, as SQLite's files are.
//
// Allocations, by operation (the counts are the cases'): append check's
// and admit's (stage's) on the operation's arenas; on the state's heap,
// the call's rows, three each, a new lane's two and its ids', the room
// each container grows to. head, forgotten, size: none. heads: the caller's
// rows. header: read_header's (canon's four and one, header's check's)
// on the operation's arena, one on the caller's. body: read_header's, then
// body's parse (canon's four and one) on the operation's arena, one on the
// caller's. scan: its lanes' room on the state's heap; each next as
// header. close and purge: none. Platform calls: pages, once at the
// object's making, once per scan and once per call given lines; part 1's
// and body's wipes, each noexcept.
//
// thread: one, each object; the state, shared (its lock).

#include <algorithm>
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

namespace state {

// The failing countdown under QIVEN3_FAULTS; elsewhere a type never made.
#if defined(QIVEN3_FAULTS)
using Countdown = memory::Countdown;
#else
struct Countdown {};
#endif

// check's scratch: at each call a fresh arena over the same pages, the
// last one's blocks all given back.
class ScratchView {
public:
    ScratchView(std::span<std::byte> pages, state::Countdown* countdown) noexcept
        : pages_(pages), countdown_(countdown) {}
    memory::Arena& operator()() {
        arena_.reset();
        arena_.emplace(pages_);
#if defined(QIVEN3_FAULTS)
        if (countdown_ != nullptr) {
            arena_->attach(countdown_);
        }
#endif
        return *arena_;
    }

private:
    std::span<std::byte> pages_;
    std::optional<memory::Arena> arena_;
    [[maybe_unused]] state::Countdown* countdown_;
};

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

}  // namespace state

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
                ++at_;
                return true;
            }
            ++lane_;
            at_ = start();
        }
        return false;
    }

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
          countdown_(other.countdown_) {
        other.gate_.close();
    }
    MemoryStore& operator=(MemoryStore&& other) noexcept {
        if (this != &other) {
            state_ = other.state_;
            platform_ = other.platform_;
            pages_ = std::move(other.pages_);
            gate_ = other.gate_;
            countdown_ = other.countdown_;
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
        state::StateView view(d, chars, op, {});
        const stage::Staged staged = stage::admit(view, scope, lane, expected, checked, state_->limits(), at, chars);
        state::Batch batch(d);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            batch.add(d, entries[i].line, entries[i].body, checked.entries[i].header());
        }
        batch.apply(d, staged.size);
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
            return false;
        }
        const Header h = state::read_header(e, op, {}, chars);
        state::check_body(*platform_, e, h, op, chars);
        record.assign(e.body);
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

    // Part 2's: refused until built.
    Written forget(std::string_view /*scope*/, std::string_view /*lane*/, Expected /*expected*/,
                   std::span<const EntryView> /*entries*/, std::span<const GuardView> /*guard*/,
                   Vector<Id>& /*erased*/) {
        gate_.enter(Operation::forget, {});
        state::unbuilt(Operation::forget);
    }

    // Done at once: a forget drops its bodies as it commits.
    Purge purge() {
        gate_.enter(Operation::purge, {});
        return Purge{};
    }

    // Part 2's: refused until built.
    state::MemoryExport export_(Scopes /*scopes*/, std::span<const AfterView> /*after*/) {
        gate_.enter(Operation::export_, {});
        state::unbuilt(Operation::export_);
    }

    // Part 2's: refused until built.
    template <Source R>
    Imported import_(R& /*source*/, Vector<LaneHead>& /*heads*/) {
        gate_.enter(Operation::import_, {});
        state::unbuilt(Operation::import_);
    }

    Sizes size() {
        gate_.enter(Operation::size, {});
        const state::Locked lock(*state_, Operation::size, {});
        Sizes s;
        s.volume = measure(lock.data().size, lock.data().count, state_->limits());
        return s;
    }

    // Part 2's: refused until built.
    Counts verify() {
        gate_.enter(Operation::verify, {});
        state::unbuilt(Operation::verify);
    }

    Purge close() {
        gate_.enter(Operation::close, {});
        gate_.close();
        return Purge{};
    }

    [[nodiscard]] bool read_only() const noexcept { return gate_.read_only(); }
    [[nodiscard]] Limits limits() const noexcept { return state_->limits(); }

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
};

}  // namespace qiven3::store
