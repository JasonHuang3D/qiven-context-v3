#pragma once

// memory_state: the in-memory store's state, which every store object the
// factory makes over it shares, and the view part 2 reads under its lock.
//
// The state: each entry held by id, as bytes (its header line, its body
// record or none, its content's digest or none), in the order appended;
// the ids sorted, for a lookup by id; each lane with its ids in seq order
// and its head; the size, the count and the forget entries held. Its
// memory is a memory::Heap over pages its maker takes from the platform
// and keeps for the state's life, its blocks freed in any order, a dropped
// body's among them; the state's containers take Allocator<T>, one Heap*,
// which raises MemoryError budget where the heap gives none. One
// memory::Mutex guards it: Locked holds it for one operation, waiting at
// most 5 s, past which StoreBusy, and gives the data while it holds it.
//
// Batch stages a call's new entries on the heap before any change, a
// lane's first among them, and reserves the room each container needs;
// apply then moves them in and moves the heads by assignments that cannot
// fail, so a refusal or an exhausted heap leaves the state as it was. A
// forget among them marks its target forgotten and drops the target's
// body; while an export is open the bytes stay in place, the entry marked
// with none, until the last export closes.
//
// StateView is part 2's view over the data, its reads on the operation's
// allocator: the head, a header (parsed again from its line), a body's
// content, the forgotten set and forget_of, the size and count; for a
// content hit, the scope's live clearings (each direction record folded at
// the entry's at, its current version not expired clearing the pattern
// for the content) and whether a header with a given content is held.
//
// Allocations. The state's on its heap: an entry's line, record and
// content digest, three; the ids' and a lane's room as they grow; a lane's
// scope and name, two. A view's on its allocator: header canon's four and
// one; body one where the buffer is short; cleared, per direction root,
// its members' room and headers, the directions' contents and the fold's
// work, on the view's scratch where given, each root's given back before
// the next (the peak one record's, whatever the records held), else on
// its allocator.
// Platform calls: none (the pages are the maker's).
//
// thread: shared; State's mutex guards its data. Locked and StateView, one.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "entry/header.hpp"
#include "entry/validator.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "memory/pool.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::state {

namespace header = entry::header;
namespace validator = entry::validator;
using Chars = memory::Allocator<char>;

// No index: an entry with no target, or no forget of it.
inline constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
// The most entries the state holds: an index fits in 32 bits.
inline constexpr std::size_t max_entries = none - 1U;
// The longest wait for the state's lock, past which StoreBusy.
inline constexpr std::chrono::seconds busy_wait{5};

// The state's heap exhausted, or a request past its type: MemoryError
// budget, naming the heap's budget and what it holds.
[[noreturn]] void exhausted(std::size_t request, const memory::Heap& heap);

// The allocator of the state's containers: one Heap*. Copies compare equal
// for one heap and propagate with the container.
template <typename T>
class Allocator {
public:
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::false_type;

    explicit Allocator(memory::Heap& heap) noexcept : heap_(&heap) {}
    template <typename U>
    Allocator(const Allocator<U>& other) noexcept : heap_(&other.heap()) {}

    [[nodiscard]] T* allocate(std::size_t n) {
        static_assert(alignof(T) <= memory::slot_align, "a heap block holds no wider alignment");
        const std::optional<std::size_t> bytes = memory::mul(n, sizeof(T));
        void* p = bytes ? heap_->allocate(*bytes) : nullptr;
        if (p == nullptr) {
            exhausted(bytes.value_or(std::numeric_limits<std::size_t>::max()), *heap_);
        }
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t /*n*/) noexcept { heap_->free(p); }
    [[nodiscard]] memory::Heap& heap() const noexcept { return *heap_; }

    template <typename U>
    friend bool operator==(const Allocator& a, const Allocator<U>& b) noexcept {
        return &a.heap() == &b.heap();
    }

private:
    memory::Heap* heap_;
};

using Bytes = std::basic_string<char, std::char_traits<char>, Allocator<char>>;
template <typename T>
using Rows = std::vector<T, Allocator<T>>;

// An entry held.
struct Held {
    explicit Held(memory::Heap& heap) noexcept
        : line(Allocator<char>(heap)), body(Allocator<char>(heap)), content(Allocator<char>(heap)) {}
    Bytes line;     // the header line, without its LF
    Bytes body;     // the body record, empty with none or once dropped
    Bytes content;  // the header's content digest, empty with none
    Id id;
    std::int64_t seq = 0;
    std::uint32_t lane = 0;      // its lane's index
    std::uint32_t target = none;  // its target's index
    std::uint32_t forget = none;  // the first forget held of it
    bool has_body = false;
    bool is_forget = false;
    bool direction = false;
    bool revision = false;  // of a kind but the change kinds, with a target
};

// A lane: its scope, its name, its entries' indices in seq order, its head.
struct Lane {
    explicit Lane(memory::Heap& heap) noexcept
        : scope(Allocator<char>(heap)), name(Allocator<char>(heap)), ids(Allocator<std::uint32_t>(heap)) {}
    Bytes scope;
    Bytes name;
    Rows<std::uint32_t> ids;
    Head head;
};

// An export open over the state: the number of the store object that made
// it, and its own.
struct Open {
    std::uint64_t owner = 0;
    std::uint64_t id = 0;
};

// The data the lock guards. The heap first: every container's blocks are
// its own, and it outlives them.
struct Data {
    explicit Data(std::span<std::byte> pages) noexcept
        : heap(pages), held(Allocator<Held>(heap)), by_id(Allocator<std::uint32_t>(heap)),
          lanes(Allocator<Lane>(heap)), exports(Allocator<Open>(heap)) {}
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    Data(Data&&) = delete;
    Data& operator=(Data&&) = delete;
    ~Data() = default;

    memory::Heap heap;
    Rows<Held> held;
    Rows<std::uint32_t> by_id;  // indices into held, sorted by id
    Rows<Lane> lanes;
    Rows<Open> exports;  // the exports open over the state
    std::uint64_t size = 0;
    std::uint64_t count = 0;
    std::uint64_t forgets = 0;
    std::uint64_t serial = 0;  // the last number given an object or an export
};

// The shared state: its lock, its data and the store's limits.
class State {
public:
    // The pages outlive the state; limits not valid() are StoreError
    // settings.
    State(std::span<std::byte> pages, const Limits& limits);
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    ~State() = default;

    [[nodiscard]] const Limits& limits() const noexcept { return limits_; }
private:
    friend class Locked;
#if defined(QIVEN3_FAULTS)
    friend void attach(State& state, memory::Countdown* countdown) noexcept;
#endif

    memory::Mutex mutex_;
    Data data_ QIVEN3_GUARDED_BY(mutex_);
    Limits limits_;
};

#if defined(QIVEN3_FAULTS)
// The state's heap asks the countdown first; null detaches it.
void attach(State& state, memory::Countdown* countdown) noexcept;
#endif

// StoreBusy past the wait.
[[noreturn]] void busy(Operation op, std::string_view path);

// Locked's wait without a deadline, for a release that cannot fail.
struct Unbounded {};

// The state's lock for one operation, and its data while held.
class QIVEN3_THREAD_ATTRIBUTE(scoped_lockable) Locked {
public:
    Locked(State& state, Operation op, std::string_view path) QIVEN3_ACQUIRE(state.mutex_)
        : mutex_(&state.mutex_) {
        if (!state.mutex_.try_lock_for(busy_wait)) {
            busy(op, path);
        }
        data_ = &state.data_;
    }
    // Waits as long as the lock is held: an export's release at its end.
    Locked(State& state, Unbounded /*wait*/) noexcept QIVEN3_ACQUIRE(state.mutex_)
        : mutex_(&state.mutex_) {
        while (!state.mutex_.try_lock_for(busy_wait)) {
        }
        data_ = &state.data_;
    }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
    Locked(Locked&&) = delete;
    Locked& operator=(Locked&&) = delete;
    ~Locked() QIVEN3_RELEASE() { mutex_->unlock(); }

    [[nodiscard]] Data& data() const noexcept { return *data_; }

private:
    memory::Mutex* mutex_;
    Data* data_ = nullptr;
};

// The index of the entry of an id, or none.
[[nodiscard]] std::uint32_t find(const Data& d, std::string_view id) noexcept;
// The index of a lane, or none.
[[nodiscard]] std::uint32_t lane_of(const Data& d, std::string_view scope, std::string_view lane) noexcept;
// Whether a scope set holds a scope: none is every scope, `project:*`
// every project scope.
[[nodiscard]] bool in_scopes(const Scopes& scopes, std::string_view scope) noexcept;
// The forget entries in a scope set.
[[nodiscard]] std::uint64_t forgets_in(const Data& d, const Scopes& scopes) noexcept;
// Each lane of a scope set and its head, in (scope, lane) order, into rows.
void heads_of(const Data& d, const Scopes& scopes, Vector<LaneHead>& rows);
// The lane's head, which must be the expected one: else HeadConflict.
std::optional<Head> expect(const Data& d, Operation op, std::string_view path, std::string_view scope,
                           std::string_view lane, Expected expected);
// A held entry's header, parsed again from its line and its id recomputed:
// a line the parser or the header's rules refuse StoreCorrupt incoherent,
// an id not the line's or not the one held StoreCorrupt id_mismatch.
[[nodiscard]] Header read_header(const Held& e, Operation op, std::string_view path, const Chars& allocator);
// A header copied onto out's allocator, its id kept: one allocation.
void copy_header(const Header& h, Header& out);
// An entry made from its line, its record or none and its header, on the
// heap: three allocations; its lane, target and forget none.
[[nodiscard]] Held make_held(Data& d, std::string_view line, std::optional<std::string_view> record,
                             const Header& h);

// A call's new entries, staged on the heap, then applied.
class Batch {
public:
    explicit Batch(Data& d) noexcept;
    // One entry, admitted: its line, its record or none, its header.
    void add(Data& d, std::string_view line, std::optional<std::string_view> record, const Header& h);
    // The room each container needs, reserved; then the entries in, the
    // heads moved, size and count added: nothing past reserve fails.
    void apply(Data& d, std::uint64_t size);
    [[nodiscard]] std::size_t entries() const noexcept { return rows_.size(); }

private:
    void reserve(Data& d);
    // Nothing here allocates: every container's room is reserved.
    void commit(Data& d, std::uint64_t size);

    Rows<Held> rows_;
    Rows<Lane> lanes_;  // lanes the call starts
    std::uint64_t forgets_ = 0;
};

// The failing countdown under QIVEN3_FAULTS; elsewhere a type never made.
#if defined(QIVEN3_FAULTS)
using Countdown = memory::Countdown;
#else
struct Countdown {};
#endif

// An arena asks the countdown first, where one is given.
inline void hook([[maybe_unused]] memory::Arena& arena, [[maybe_unused]] Countdown* countdown) noexcept {
#if defined(QIVEN3_FAULTS)
    if (countdown != nullptr) {
        arena.attach(countdown);
    }
#endif
}

// check's scratch, then the clearing's: at each call a fresh arena over
// the same pages, the last one's blocks all given back.
class ScratchView {
public:
    ScratchView(std::span<std::byte> pages, Countdown* countdown) noexcept : pages_(pages), countdown_(countdown) {}
    memory::Arena& operator()() {
        if (arena_) {
            peak_ = std::max(peak_, arena_->peak());
        }
        arena_.reset();
        arena_.emplace(pages_);
        hook(*arena_, countdown_);
        return *arena_;
    }
    // The greatest high-water mark of its arenas so far.
    [[nodiscard]] std::size_t peak() const noexcept { return std::max(peak_, arena_ ? arena_->peak() : 0); }

private:
    std::span<std::byte> pages_;
    std::optional<memory::Arena> arena_;
    Countdown* countdown_;
    std::size_t peak_ = 0;
};

// Part 2's view over the data, while the lock is held.
class StateView {
public:
    // scratch: where given, cleared's work, afresh for each direction root.
    StateView(Data& data, const Chars& allocator, Operation op, std::string_view path,
              ScratchView* scratch = nullptr) noexcept
        : data_(&data), allocator_(allocator), path_(path), op_(op), scratch_(scratch) {}

    [[nodiscard]] std::optional<Head> head(std::string_view scope, std::string_view lane);
    bool header(std::string_view id, Header& out);
    bool body(std::string_view id, validator::Text<Chars>& out);
    [[nodiscard]] bool forgotten(std::string_view id);
    [[nodiscard]] std::optional<Id> forget_of(std::string_view id);
    [[nodiscard]] bool cleared(std::string_view scope, std::string_view content, std::string_view pattern,
                               std::string_view at);
    [[nodiscard]] bool holds_content(std::string_view content);
    [[nodiscard]] std::uint64_t size() const noexcept { return data_->size; }
    [[nodiscard]] std::uint64_t count() const noexcept { return data_->count; }

private:
    // Whether direction record root clears a content for a pattern at at:
    // its work on a fresh scratch arena, else on the view's allocator.
    bool clears(std::uint32_t root, std::string_view scope, std::string_view content, std::string_view pattern,
                std::string_view at);

    Data* data_;
    Chars allocator_;
    std::string_view path_;
    Operation op_;
    ScratchView* scratch_;
};

// The pinned sizes, on x64 with MSVC's library in its release layout.
static_assert(sizeof(Allocator<char>) == 8, "the state's allocator is pinned: one Heap*");
static_assert(sizeof(Open) == 16, "Open's size is pinned: two numbers");
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Held) == 216, "Held's size is pinned: three texts, an Id, seq, three indices and three flags");
static_assert(sizeof(Lane) == 192, "Lane's size is pinned: two texts, the ids and a Head");
#endif

}  // namespace qiven3::store::state
