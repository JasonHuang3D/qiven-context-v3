#pragma once

// values: the plain values of the store contract (contract.hpp), which a
// header can read without its concepts or Gate: the operations, by the
// text's names, and the kinds each raises; the capabilities; the limits
// and the volume measured against them; the results, append's to size's;
// a record's keys and an export's or an import's line, owned on the
// caller's allocator; the views a call is given; and the budgets.
//
// The budgets, as the text states them: each operation but an export
// 1 MiB plus 4 times the lines given, the engine's heap apart (2,000 KiB
// of cache plus 1 MiB a connection); an export's iteration 2 MiB.
//
// Allocations: none; Line's three strings and Record's are empty until
// written. Platform calls: none. thread: none.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "keys/keys.hpp"
#include "memory/memory.hpp"
#include "store/errors.hpp"

namespace qiven3::store {

using keys::Head, keys::Id, keys::in_scope, keys::in_scopes, keys::KeysView, keys::max_call_entries, keys::Scopes;
using memory::String, memory::Vector;

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
// its LF, and of each kind; the count, the header lines; and their state: past the target,
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

// A kind's bytes, a key's bound as the scope's and the lane's.
inline constexpr std::size_t kind_bytes = 32;

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
    // bound (Id's capacity, the scope's, the lane's or the kind's bytes).
    [[nodiscard]] bool assign(const KeysView& k) {
        const auto fits = [](const std::optional<std::string_view>& i) { return !i || Id::fits(*i); };
        if (!Id::fits(k.id) || k.scope.size() > scope_bytes || k.lane.size() > lane_bytes ||
            k.kind.size() > kind_bytes || !fits(k.parent) || !fits(k.target)) {
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
// The import's pages for the line in hand, a quarter of an operation's;
// an entry's bytes (its header line, its body record and its keys) at
// most a KiB under them, room for its texts' allocations, so that every
// entry a store holds moves by its own export and import.
inline constexpr std::size_t import_line_bytes = operation_base / 4;
inline constexpr std::size_t entry_bytes = import_line_bytes - kib;
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

// The pinned sizes. The values on 64-bit targets; Line's on x64 with
// MSVC's library in its release layout.
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
#endif

}  // namespace qiven3::store
