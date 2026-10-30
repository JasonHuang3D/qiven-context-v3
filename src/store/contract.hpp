#pragma once

// contract: the store contract, bound at compile time. A store is a
// concrete type satisfying Store, with no base class and no virtual
// function; code that uses a store is a template on it. Two stores meet
// it, SQLite and the in-memory reference store, and a later store passes
// the suite before it is ruled in.
//
// The operations, by the text's names (export_ and import_ for export and
// import, which C++ reserves): append, head, heads, header, body,
// forgotten, scan, forget, purge, export_, import_, size, verify, close;
// read_only() and limits() on a const object; and the type's capabilities,
// durable, files and processes, as a constant. Views in, the caller's
// buffers out: ids, scopes, lanes and lines enter as std::string_view, an
// entry as EntryView (its header line and its body record or none), and a
// result is a value type here or is written into the String, Header or
// Vector the caller gives, on the caller's arena. A header line, a body
// record, a scan's records and an export's lines are given as their bytes
// and keys; the store parses none of them.
//
// A scan and an export are handles of their own: move-only values, read
// one entry at a time by next(), each ended by its destructor. An export
// gives its snapshot's heads, forget count, size and count first, then its
// lines; an import reads any Source of that shape, an export among them.
//
// A purge pending is a result with its reason, never an error. The store's
// own errors are StoreError (errors.hpp), of the kinds raises() gives;
// beside them an operation raises MemoryError budget for an allocation
// refused within it, the engine's heap's among them, never a StoreError,
// and a platform call's PlatformError as the platform gives it (pages,
// resolve, synced, create). Gate is
// the check each store runs first: the thread that made the object, then
// closed, then read-only. Hooks are the step hooks, begun, written,
// committed and rebuilt, which do nothing in NoHooks; a store takes its
// hooks as a template parameter, so a seam supplies its own type.
//
// The budgets, as the text states them: each operation but an export
// 1 MiB plus 4 times the lines given, the engine's heap apart (2,000 KiB
// of cache plus 1 MiB a connection); an export's iteration 2 MiB.
//
// Allocations: none; Line's three strings and Record's are empty until
// written. Platform
// calls: none. thread: Gate reads the calling thread's id; the rest none.

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>

#include "entry/header.hpp"
#include "entry/validator.hpp"
#include "memory/memory.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"

namespace qiven3::store {

using Header = entry::header::Header<memory::Allocator<char>>;
using memory::String;
using memory::Vector;

// The operations, and their names as the text spells them.
enum class Operation : std::uint8_t {
    append, head, heads, header, body, forgotten, scan, forget, purge, export_, import_, size, verify, close
};
inline constexpr std::array<std::string_view, 14> operation_names = {
    "append", "head",   "heads",  "header", "body", "forgotten", "scan",
    "forget", "purge",  "export", "import", "size", "verify",    "close"};
constexpr std::string_view name(Operation op) noexcept { return operation_names[static_cast<std::size_t>(op)]; }

// What a read-only store serves; any other operation is StoreError
// read_only.
constexpr bool serves_read_only(Operation op) noexcept {
    return op != Operation::append && op != Operation::forget && op != Operation::purge && op != Operation::import_;
}

// The kinds an operation raises: every one StoreBusy, StoreLocked,
// StoreCorrupt and StoreError; append and forget HeadConflict; append,
// forget and import EntryInvalid and StoreFull; header, body and forgotten
// NotFound; import StoreNotEmpty.
constexpr bool raises(Operation op, Kind kind) noexcept {
    const bool writes = op == Operation::append || op == Operation::forget;
    switch (kind) {
    case Kind::store_error:
    case Kind::busy:
    case Kind::locked:
    case Kind::corrupt:
        return true;
    case Kind::head_conflict:
        return writes;
    case Kind::entry_invalid:
    case Kind::full:
        return writes || op == Operation::import_;
    case Kind::not_found:
        return op == Operation::header || op == Operation::body || op == Operation::forgotten;
    case Kind::not_empty:
        return op == Operation::import_;
    }
    return false;
}
// StoreFull's codes: store_full at append alone, a forget and an import
// never refused for size or count; disk_full wherever StoreFull is.
constexpr bool raises(Operation op, Full code) noexcept {
    return code == Full::store_full ? op == Operation::append : raises(op, Kind::full);
}

// The capabilities, which each store type declares as the constant
// `capabilities`: durable, an append survives the process that made it;
// files, its state lives in files; processes, several processes can open it
// at once. A case needing one the store lacks is not applicable.
struct Capabilities {
    bool durable = false;
    bool files = false;
    bool processes = false;
    constexpr bool operator==(const Capabilities&) const = default;
};
constexpr bool applicable(const Capabilities& have, const Capabilities& need) noexcept {
    return (have.durable || !need.durable) && (have.files || !need.files) && (have.processes || !need.processes);
}

// The thresholds by size and by count, the store's limits: the target,
// the warning and the maximum, the format's by default and smaller in
// the suite.
inline constexpr std::uint64_t mib = std::uint64_t{1024} * 1024U;
struct Limits {
    std::uint64_t target_size = 512 * mib;
    std::uint64_t warning_size = 640 * mib;
    std::uint64_t max_size = 768 * mib;
    std::uint64_t target_count = 400'000;
    std::uint64_t warning_count = 480'000;
    std::uint64_t max_count = 550'000;
    constexpr bool operator==(const Limits&) const = default;
};
// Each nonzero and each threshold at or under the next.
constexpr bool valid(const Limits& l) noexcept {
    return l.target_size > 0 && l.target_size <= l.warning_size && l.warning_size <= l.max_size &&
           l.target_count > 0 && l.target_count <= l.warning_count && l.warning_count <= l.max_count;
}

// The size, the bytes of every header line and body record held, each with
// its LF; the count, the header lines; and their state: past the target,
// past the warning, by size or by count.
struct Volume {
    std::uint64_t size = 0;
    std::uint64_t count = 0;
    bool over_target = false;
    bool over_warning = false;
    constexpr bool operator==(const Volume&) const = default;
};
constexpr Volume measure(std::uint64_t size, std::uint64_t count, const Limits& l) noexcept {
    return {size, count, size > l.target_size || count > l.target_count,
            size > l.warning_size || count > l.warning_count};
}
// Whether a size and count are past the maximum, by either: an append that
// would take them there is StoreFull store_full.
constexpr bool over_maximum(std::uint64_t size, std::uint64_t count, const Limits& l) noexcept {
    return size > l.max_size || count > l.max_count;
}

// A purge: done, or pending and its reason: writer, reader, locked or the
// engine's code's name.
inline constexpr std::size_t reason_bytes = 32;
struct Purge {
    memory::Inline<reason_bytes> reason;
    bool done = true;
    static constexpr Purge pending(std::string_view why) noexcept { return {memory::Inline<reason_bytes>(why), false}; }
};

// A lane's head, with its scope and lane by value.
struct LaneHead {
    Head head;
    memory::Inline<scope_bytes> scope;
    memory::Inline<lane_bytes> lane;
};

// append's and forget's result: the new head, the size and the count with
// their flags, and the purge. forget writes the ids it erased apart.
struct Written {
    Head head;
    Volume volume;
    Purge purge;
};
// heads' result beside its rows: the forget entries in the scope set, or in
// the whole store without one, and the purge.
struct Heads {
    std::uint64_t forgets = 0;
    Purge purge;
};
// An export's snapshot beside its heads: the set's forget count, the size
// and the count.
struct Snapshot {
    std::uint64_t forgets = 0;
    Volume volume;
};
// import's and verify's counts: header lines, body records, forget entries
// and lanes.
struct Counts {
    std::uint64_t entries = 0;
    std::uint64_t bodies = 0;
    std::uint64_t forgets = 0;
    std::uint64_t lanes = 0;
    constexpr bool operator==(const Counts&) const = default;
};
// import's result beside the heads it wrote.
struct Imported {
    Counts counts;
    Volume volume;
};
// size's result: the volume and the implementation's own figures, each
// named (SQLite's page_size, page_count, freelist_count, its files' sizes).
inline constexpr std::size_t figure_bytes = 24;
inline constexpr std::size_t figures_max = 8;
struct Figure {
    memory::Inline<figure_bytes> name;
    std::uint64_t value = 0;
};
struct Sizes {
    Volume volume;
    std::array<Figure, figures_max> detail{};
    std::uint8_t figures = 0;
    [[nodiscard]] constexpr std::span<const Figure> view() const noexcept { return {detail.data(), figures}; }
};

// A record's keys owned: its ids by value, its scope and lane in place,
// its kind on the caller's allocator.
struct Keys {
    explicit Keys(const memory::Allocator<char>& allocator) noexcept : kind(allocator) {}
    Id id;
    memory::Inline<scope_bytes> scope;
    memory::Inline<lane_bytes> lane;
    String kind;
    std::int64_t seq = 0;
    std::optional<Id> parent, target;
    bool body = false;  // a body, held or forgotten

    // Its keys, views of it.
    [[nodiscard]] KeysView view() const noexcept {
        const auto text = [](const std::optional<Id>& i) {
            return i ? std::optional<std::string_view>(i->view()) : std::nullopt;
        };
        return KeysView{id.view(), scope.view(), lane.view(), kind, seq, text(parent), text(target), body};
    }
    // k made its own; false, and nothing changed, where a key is past its
    // bound (Id's capacity, the scope's or the lane's bytes).
    [[nodiscard]] bool assign(const KeysView& k) {
        const auto fits = [](const std::optional<std::string_view>& i) { return !i || Id::fits(*i); };
        if (!Id::fits(k.id) || k.scope.size() > scope_bytes || k.lane.size() > lane_bytes || !fits(k.parent) ||
            !fits(k.target)) {
            return false;
        }
        const auto of = [](const std::optional<std::string_view>& i) {
            return i ? std::optional<Id>(Id::of(*i)) : std::nullopt;
        };
        kind.assign(k.kind);
        id = Id::of(k.id);
        scope = memory::Inline<scope_bytes>(k.scope);
        lane = memory::Inline<lane_bytes>(k.lane);
        seq = k.seq;
        parent = of(k.parent);
        target = of(k.target);
        body = k.body;
        return true;
    }
};

// An export's or an import's line: a header line, its body record or none,
// whether a forget targets it, and its keys (an export's the store's, an
// import's NoAdmit's to give); its text on the caller's arena.
struct Line {
    explicit Line(memory::Arena& arena) noexcept
        : header(memory::Allocator<char>(arena)), body(memory::Allocator<char>(arena)),
          keys(memory::Allocator<char>(arena)) {}
    String header;
    String body;
    bool has_body = false;
    bool forgotten = false;
    Keys keys;
};

// The views a call is given, valid for the call alone.
struct EntryView {
    std::string_view line;                 // the stored header line
    std::optional<std::string_view> body;  // its body record, or none
    KeysView keys;                         // its keys, NoAdmit's to give
};

// A scan's record: its keys owned and its header line's bytes, on the
// caller's allocator.
struct Record {
    explicit Record(const memory::Allocator<char>& allocator) noexcept : kind(allocator), line(allocator) {}
    Id id;
    memory::Inline<scope_bytes> scope;
    memory::Inline<lane_bytes> lane;
    String kind;
    std::int64_t seq = 0;
    std::optional<Id> parent, target;
    bool body = false;  // a body, held or forgotten
    String line;        // the header line, without its LF

    // Its keys, views of it.
    [[nodiscard]] KeysView keys() const noexcept {
        const auto text = [](const std::optional<Id>& i) {
            return i ? std::optional<std::string_view>(i->view()) : std::nullopt;
        };
        return KeysView{id.view(), scope.view(), lane.view(), kind, seq, text(parent), text(target), body};
    }
};
struct GuardView {  // a head the store holds, as the caller expects it
    std::string_view scope, lane, id;
};
struct AfterView {  // an export's seq to start after, for one lane
    std::string_view scope, lane;
    std::int64_t seq = 0;
};
struct RangeView {  // a scan's scope, lane and seq to start after, each optional
    std::optional<std::string_view> scope, lane;
    std::optional<std::int64_t> after;
};
// The expected head: an id, or none for an empty lane.
using Expected = std::optional<std::string_view>;

// The budgets, in bytes.
inline constexpr std::size_t kib = 1024;
inline constexpr std::size_t operation_base = kib * kib;
inline constexpr std::size_t lines_factor = 4;
inline constexpr std::size_t engine_budget = (2000 * kib) + (kib * kib);
inline constexpr std::size_t export_budget = 2 * kib * kib;
// The bytes of the lines a call is given: each header line and body
// record; none past SIZE_MAX.
constexpr std::optional<std::size_t> given(std::span<const EntryView> entries) noexcept {
    std::size_t total = 0;
    for (const EntryView& e : entries) {
        const std::optional<std::size_t> line = memory::add(total, e.line.size());
        const std::optional<std::size_t> next = line ? memory::add(*line, e.body ? e.body->size() : 0) : line;
        if (!next) {
            return std::nullopt;
        }
        total = *next;
    }
    return total;
}
// An operation's budget for the lines it is given: 1 MiB plus 4 times them.
constexpr std::optional<std::size_t> operation_budget(std::size_t lines) noexcept {
    const std::optional<std::size_t> times = memory::mul(lines, lines_factor);
    return times ? memory::add(operation_base, *times) : times;
}

// Each operation's first check, in this order: a call from another thread
// than the one that made the object is StoreError thread; after close,
// StoreError closed; on a read-only object, an operation it does not serve
// StoreError read_only. Each names the operation and the store's path.
// own is the first alone: an export's close, again after close nothing.
class Gate {
public:
    explicit Gate(bool read_only) noexcept : owner_(std::this_thread::get_id()), read_only_(read_only) {}
    void own(Operation op, std::string_view path) const {
        if (std::this_thread::get_id() != owner_) {
            throw StoreError(Code::thread, name(op), path);
        }
    }
    void enter(Operation op, std::string_view path) const {
        own(op, path);
        if (closed_) {
            throw StoreError(Code::closed, name(op), path);
        }
        if (read_only_ && !serves_read_only(op)) {
            throw StoreError(Code::read_only, name(op), path);
        }
    }
    void close() noexcept { closed_ = true; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] bool read_only() const noexcept { return read_only_; }

private:
    decltype(std::this_thread::get_id()) owner_;
    bool read_only_;
    bool closed_ = false;
};

// A move-only value: a store object, a scan or an export.
template <class H>
concept Handle = std::movable<H> && !std::copyable<H>;

// The step hooks: after BEGIN IMMEDIATE, after the writes, after COMMIT,
// and once a purge's VACUUM returns.
template <class H>
concept Hooks = requires(H& h) {
    h.begun();
    h.written();
    h.committed();
    h.rebuilt();
};
struct NoHooks {
    constexpr void begun() const noexcept {}
    constexpr void written() const noexcept {}
    constexpr void committed() const noexcept {}
    constexpr void rebuilt() const noexcept {}
};

// The call a refusal names: its operation and the store's path.
struct AtView {
    Operation op = Operation::append;
    std::string_view path;
};

// The admit hook of append and forget: a product's checks of the entries a
// call is given, on the store's work pages (kept, the call's; scratch, a
// fresh arena over the same pages each step), each refusal thrown. given
// before any entry is read; read first with each line, its keys into out
// (views valid until release), and kept once the store keeps it, both
// before the store's transaction or lock; admit inside it, after the head
// and the guard, with the store's read view, before any write; release,
// however the call ends, before the store's pages go, gives back what the
// hook holds on them. P is the store's platform, V its view, E its kept
// entry, S its scratch.
template <class A, class P, class V, class E, class S>
concept Admit = requires(A& a, P& platform, V& view, const AtView& at, std::span<const EntryView> lines, std::size_t i,
                         const EntryView& line, KeysView& out, const E& e, std::span<const E> entries,
                         std::string_view v, Expected expected, const memory::Allocator<char>& kept, S& scratch) {
    a.given(at, lines, kept, scratch);
    a.read(platform, at, i, line, out, kept, scratch);
    a.kept(at, i, e, kept, scratch);
    a.admit(view, v, v, expected, entries, at, kept);
    { a.release() } noexcept;
};

// The hook's release at the end of the scope it is made in, after the
// store's pages are made and before they go.
template <class A>
class Released {
public:
    explicit Released(A& a) noexcept : a_(&a) {}
    Released(const Released&) = delete;
    Released& operator=(const Released&) = delete;
    Released(Released&&) = delete;
    Released& operator=(Released&&) = delete;
    ~Released() { a_->release(); }

private:
    A* a_;
};

// The admit hook of import, inside its transaction or lock, each line's
// and entry's work on a fresh arena (work): read first with each line, its
// keys into out (views valid until release); arrived with the line and
// those keys before its insert; settled with each entry in the final view,
// its line, body record and keys as the store holds them; heads with the
// source's heads and those the entries give. release, however each line's
// step ends, before its arena goes, gives back what the hook holds on it.
// P is the store's platform, V its view.
template <class A, class P, class V>
concept AdmitImport = requires(A& a, P& platform, V& view, const AtView& at, std::size_t i, const EntryView& line,
                               KeysView& out, const memory::Allocator<char>& work, std::span<const LaneHead> heads) {
    a.read(platform, at, i, line, out, work);
    a.arrived(view, line, i, at);
    a.settled(view, line, i, at, work);
    a.heads(heads, heads, at);
    { a.release() } noexcept;
};

// The hook that admits what the store's own rules admit: each line's keys
// as its caller gives them.
struct NoAdmit {
    template <class S>
    constexpr void given(const AtView& /*at*/, std::span<const EntryView> /*lines*/,
                         const memory::Allocator<char>& /*kept*/, S& /*scratch*/) const noexcept {}
    template <class P, class S>
    constexpr void read(P& /*platform*/, const AtView& /*at*/, std::size_t /*index*/, const EntryView& line,
                        KeysView& out, const memory::Allocator<char>& /*kept*/, S& /*scratch*/) const noexcept {
        out = line.keys;
    }
    template <class E, class S>
    constexpr void kept(const AtView& /*at*/, std::size_t /*index*/, const E& /*e*/,
                        const memory::Allocator<char>& /*kept*/, S& /*scratch*/) const noexcept {}
    template <class V, class E>
    constexpr void admit(V& /*view*/, std::string_view /*scope*/, std::string_view /*lane*/, Expected /*expected*/,
                         std::span<const E> /*entries*/, const AtView& /*at*/,
                         const memory::Allocator<char>& /*kept*/) const noexcept {}
    template <class P>
    constexpr void read(P& /*platform*/, const AtView& /*at*/, std::size_t /*index*/, const EntryView& line,
                        KeysView& out, const memory::Allocator<char>& /*work*/) const noexcept {
        out = line.keys;
    }
    template <class V>
    constexpr void arrived(V& /*view*/, const EntryView& /*line*/, std::size_t /*index*/,
                           const AtView& /*at*/) const noexcept {}
    template <class V>
    constexpr void settled(V& /*view*/, const EntryView& /*line*/, std::size_t /*index*/, const AtView& /*at*/,
                           const memory::Allocator<char>& /*work*/) const noexcept {}
    constexpr void heads(std::span<const LaneHead> /*source*/, std::span<const LaneHead> /*given*/,
                         const AtView& /*at*/) const noexcept {}
    constexpr void release() const noexcept {}
};

// The hook a store of Rule takes at a write: any where Rule is NoAdmit,
// Rule's alone otherwise, so that no call swaps a ruled store's rules.
template <class A, class Rule>
concept HookOf = std::same_as<Rule, NoAdmit> || std::same_as<std::remove_cvref_t<A>, Rule>;

// A scan's records in (scope, lane, seq) order; false at the end. Every
// entry committed before the scan began comes once, and none after: each
// lane ends at its length at the scan's start.
template <class H>
concept Scan = Handle<H> && requires(H& h, Record& out) {
    { h.next(out) } -> std::same_as<bool>;
};

// An import's source: its heads, then its lines in (scope, lane, seq)
// order; false at the end.
template <class R>
concept Source = requires(R& r, Vector<LaneHead>& rows, Line& line) {
    r.heads(rows);
    { r.next(line) } -> std::same_as<bool>;
};

// An export: a Source whose heads give the snapshot's count and volume,
// and which closes.
template <class E>
concept Export = Handle<E> && Source<E> && requires(E& e, Vector<LaneHead>& rows) {
    { e.heads(rows) } -> std::same_as<Snapshot>;
    e.close();
};

namespace detail {
// The source the concept imports from: an import is a template on Source.
struct SourceArchetype {
    static void heads(Vector<LaneHead>& /*rows*/) noexcept {}
    [[nodiscard]] static bool next(Line& /*line*/) noexcept { return false; }
};
// The capabilities, as a constant of their type.
template <class S>
concept Declares = requires {
    { S::capabilities } -> std::same_as<const Capabilities&>;
    typename std::integral_constant<bool, S::capabilities.durable>;
};
}  // namespace detail

// The contract.
template <class S>
concept Store = Handle<S> && detail::Declares<S> &&
                requires(S& s, const S& c, std::string_view v, Expected expected, std::span<const EntryView> entries,
                         std::span<const GuardView> guard, Scopes scopes, std::span<const AfterView> after,
                         const RangeView& range, String& line, String& record, Vector<LaneHead>& rows,
                         Vector<Id>& erased, detail::SourceArchetype& source) {
                    { s.append(v, v, expected, entries) } -> std::same_as<Written>;
                    { s.head(v, v) } -> std::same_as<std::optional<Head>>;
                    { s.heads(scopes, rows) } -> std::same_as<Heads>;
                    { s.header(v, line) } -> std::same_as<void>;
                    { s.body(v, record) } -> std::same_as<bool>;  // false: forgotten, or a forget
                    { s.forgotten(v) } -> std::same_as<bool>;
                    { s.scan(range) } -> Scan;
                    { s.forget(v, v, expected, entries, guard, erased) } -> std::same_as<Written>;
                    { s.purge() } -> std::same_as<Purge>;
                    { s.export_(scopes, after) } -> Export;
                    { s.import_(source, rows) } -> std::same_as<Imported>;
                    { s.size() } -> std::same_as<Sizes>;
                    { s.verify() } -> std::same_as<Counts>;
                    { s.close() } -> std::same_as<Purge>;
                    { c.read_only() } -> std::same_as<bool>;
                    { c.limits() } -> std::same_as<Limits>;
                };

// The pinned sizes. The values on 64-bit targets; Line's on x64 with
// MSVC's library in its release layout.
static_assert(std::is_empty_v<NoHooks>, "the default hooks take no space");
static_assert(std::is_empty_v<NoAdmit>, "the store's own admit takes no space");
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Volume) == 24, "Volume's size is pinned: two counts and two flags");
static_assert(sizeof(Purge) == 48, "Purge's size is pinned: its reason in place and done");
static_assert(sizeof(LaneHead) == 184, "LaneHead's size is pinned: a Head, a scope and a lane in place");
static_assert(sizeof(Written) == 152, "Written's size is pinned: a Head, a Volume and a Purge");
static_assert(sizeof(Heads) == 56, "Heads' size is pinned: a count and a Purge");
static_assert(sizeof(Snapshot) == 32, "Snapshot's size is pinned: a count and a Volume");
static_assert(sizeof(Counts) == 32, "Counts' size is pinned: four counts");
static_assert(sizeof(Imported) == 56, "Imported's size is pinned: Counts and a Volume");
static_assert(sizeof(Sizes) == 352, "Sizes' size is pinned: a Volume and eight figures");
static_assert(sizeof(Limits) == 48, "Limits' size is pinned: six thresholds");
#endif
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Keys) == 376, "Keys' size is pinned: three Ids, a scope and a lane in place, a kind and seq");
static_assert(sizeof(Line) == 464, "Line's size is pinned: two strings, two flags and Keys");
static_assert(sizeof(Gate) == 8, "Gate's size is pinned: a thread's id and two flags");
#endif

}  // namespace qiven3::store
