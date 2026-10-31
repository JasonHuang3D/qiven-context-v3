#pragma once

// sqlite: what the SQLite store runs through its connection wrapper and
// engine heap (connection.hpp, which this header includes): the open steps,
// the writes, the reads and the views over them. In the store's own
// directory, sqlite_open.cpp holds the open steps, through the wrapper,
// sqlite_write.cpp the writes, sqlite_read.cpp the reads; sqlite_store.hpp
// the store itself, a template on the platform.
//
// Allocations: on the engine heap alone (connection.hpp), and the open
// steps' std::filesystem paths (presence, the checkout walk), which the
// standard library allocates. thread: as connection.hpp states.

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/engine/connection.hpp"
#include "store/errors.hpp"

namespace qiven3::store::sqlite {

// What open read: the meta rows, and the entry and body rows counted.
struct Opened {
    std::int64_t size = 0;
    std::int64_t count = 0;
    std::int64_t forgets = 0;
    std::int64_t vacuumed = 0;
    std::int64_t purged = 0;
    std::int64_t entries = 0;
    std::int64_t bodies = 0;
    // A purge is pending while purged is below forgets.
    [[nodiscard]] bool pending() const noexcept { return purged < forgets; }
};

// The open steps (sqlite_open.cpp), each given the resolved path.
// Whether path, or a directory above it, holds .git, a file or a directory.
[[nodiscard]] bool in_checkout(std::string_view path);
// Step 2's checks of the file: at open, none is StoreError missing and
// one of 0 bytes StoreCorrupt corrupt; at create, any is StoreError exists.
void presence(std::string_view path, bool create, const WhereView& at);
// The directory of path, by its last separator, and whether it is one.
[[nodiscard]] std::string_view directory_of(std::string_view path) noexcept;
[[nodiscard]] bool is_directory(std::string_view path);
// Step 3: application_id and user_version; another StoreCorrupt foreign or
// version.
void identify(Connection& c, const WhereView& at);
// Step 4: each setting written (on a read-only connection, all but
// journal_mode), then each read back; the first that differs is StoreError
// settings, its name and the value read in detail.
void apply(Connection& c, bool read_only, const WhereView& at);
// The first setting whose value read back differs, by its index in
// settings; the identity apart, which identify reads. None: every one
// holds.
[[nodiscard]] std::optional<std::size_t> differing(Connection& c, const WhereView& at);
// Step 5: the schema, the meta rows and the identity in one BEGIN
// IMMEDIATE transaction, then a TRUNCATE checkpoint.
void create_schema(Connection& c, const WhereView& at);
// Step 6: BEGIN IMMEDIATE, a write that changes nothing, ROLLBACK.
void probe(Connection& c, const WhereView& at);
// Step 7: coherence, in one read transaction; StoreCorrupt incoherent or
// forgotten_body.
[[nodiscard]] Opened coherence(Connection& c, const WhereView& at);
// A refused create's removals refused, a bit each in removal_names' order.
using Refused = std::uint8_t;
inline constexpr std::array<std::string_view, 5> removal_names = {"-journal", "-wal", "-shm", "the file",
                                                                  "the directory"};
inline constexpr Refused directory_refused = 1U << 4U;
// A refused create's files removed: its -journal, -wal and -shm, each
// tried, then the file once none of them is left, so that no later file
// at path meets them; the removals refused.
[[nodiscard]] Refused unmake(std::string_view path) noexcept;
// In a handler: the error in flight raised again, a StoreError's,
// MemoryError's or PlatformError's detail followed by "; removal refused: "
// and each refused's name, as much as its detail holds; any other as it is.
[[noreturn]] void reraise(Refused refused);
// Steps 2 (the connection) to 7 on a file presence() passed, or that
// create made (sqlite_create.hpp's made, which undoes a refused one); the
// connection closed as a step's error leaves.
[[nodiscard]] Connection connect(const memory::String& path, bool create, bool read_only, const WhereView& at,
                                 Opened& opened);

// A file's size in bytes, the path's UTF-8 bytes and a suffix after them
// ("-wal", "-shm"); none when absent, not a regular file or unreadable
// (sizes/sizes.cpp).
[[nodiscard]] std::optional<std::uint64_t> file_bytes(std::string_view path, std::string_view suffix = {});

// The writes (sqlite_write.cpp), through the wrapper.
using Chars = memory::Allocator<char>;
#if defined(QIVEN_FAULTS)
using Countdown = memory::Countdown;
// The process's two countdowns: calls, which each engine call the wrapper
// maps asks first, a failed one taking SQLITE_IOERR as its result, the
// engine not called; and arenas, which a store object made while it is
// attached takes for its arenas, open's and the guard's among them. None,
// nullptr.
void attach_calls(Countdown* calls, Countdown* arenas) noexcept;
[[nodiscard]] Countdown* arenas_countdown() noexcept;
#else
struct Countdown {};
#endif

// stage's scratch: at each call a fresh arena over the same pages, the
// last one's blocks all given back; under QIVEN_FAULTS each asks the
// countdown, where one is given.
class ScratchView {
public:
    ScratchView(std::span<std::byte> pages, Countdown* countdown) noexcept : pages_(pages), countdown_(countdown) {}
    memory::Arena& operator()();
    // The greatest high-water mark of its arenas so far.
    [[nodiscard]] std::size_t peak() const noexcept;

private:
    std::span<std::byte> pages_;
    std::optional<memory::Arena> arena_;
    [[maybe_unused]] Countdown* countdown_;
    std::size_t peak_ = 0;
};

// The size and the count meta holds, read in the caller's transaction.
struct Totals {
    std::int64_t size = 0;
    std::int64_t count = 0;
};
[[nodiscard]] Totals totals(Connection& c, const WhereView& at);

// The view's generic reads under the write lock, each planned with no
// structure built in memory as large as its rows (S4's plan check): a
// scope's ids of a kind, a target or none, the ids targeting an id in a
// scope, a held line, an id held in a scope, and the scan of entry.line
// for bytes.
inline constexpr std::string_view keyed_roots =
    "SELECT id FROM entry WHERE scope = ?1 AND kind = ?2 ORDER BY lane, seq";
inline constexpr std::string_view record_members =
    "SELECT id FROM entry WHERE target = ?1 AND scope = ?2 ORDER BY rowid";
inline constexpr std::string_view target_members = "SELECT id FROM entry WHERE target = ?1 ORDER BY rowid";
inline constexpr std::string_view held_line = "SELECT line FROM entry WHERE id = ?1";
inline constexpr std::string_view held_scope = "SELECT EXISTS (SELECT 1 FROM entry WHERE id = ?1 AND scope = ?2)";
inline constexpr std::string_view content_scan = "SELECT EXISTS (SELECT 1 FROM entry WHERE instr(line, ?1) > 0)";

// The generic calls' reads on c over those statements (sqlite_read.cpp),
// the view's and the store's own outside a write: a record's line into
// out, false where none is held; the ids whose target is id, of *scope
// where given, into out, cleared first, in the order written; f(id) for
// each id of scope and kind, a target or none, in (lane, seq) order, until f
// gives true: whether it did, no list held, the statement's cursor walked.
bool line_of(Connection& c, std::string_view id, String& out, const WhereView& at);
void targets_of(Connection& c, std::string_view id, const std::string_view* scope, Vector<Id>& out,
                const WhereView& at);
template <class F>
bool keyed_of(Connection& c, std::string_view scope, std::string_view kind, F& f, const WhereView& at) {
    Statement s = c.prepare(keyed_roots, at);
    s.bind_text(1, scope, at);
    s.bind_text(2, kind, at);
    while (s.step(at)) {
        if (f(Id::of(s.text(0)))) {
            return true;
        }
    }
    return false;
}

// Part 2's view of the transaction (stage::View): the head, a header line's
// and a body record's bytes by id, the forgotten set, the generic calls by
// the keys held (a line, targets, keyed ids, each walked on its cursor by
// each_keyed, a byte search), a work allocator (the scratch's next arena,
// else the view's allocator), the size and the count the transaction began
// with.
class WriteView {
public:
    WriteView(Connection& c, const Chars& allocator, ScratchView* scratch, const WhereView& at,
              const Totals& held) noexcept
        : c_(&c), allocator_(allocator), scratch_(scratch), at_(at), held_(held) {}

    [[nodiscard]] std::optional<Head> head(std::string_view scope, std::string_view lane);
    bool header(std::string_view id, String& out);
    bool body(std::string_view id, String& out);
    [[nodiscard]] bool forgotten(std::string_view id);
    // The generic calls, by the keys the store holds: a record's line by
    // id, false where none is held; the ids whose target is id, in scope
    // or in every scope; a scope's ids of kind, a target or none, in
    // (lane, seq) order; each list into out, cleared first, the targets' in the
    // order written (rowid); whether a held line holds bytes; whether a
    // record of id is held, in any scope or in scope.
    bool line(std::string_view id, String& out);
    [[nodiscard]] bool held(std::string_view id);
    [[nodiscard]] bool held_in(std::string_view id, std::string_view scope);
    void targets(std::string_view id, std::string_view scope, Vector<Id>& out);
    void targets(std::string_view id, Vector<Id>& out);
    void keyed(std::string_view scope, std::string_view kind, Vector<Id>& out);
    [[nodiscard]] bool contains(std::string_view bytes);
    // f(id) for each id keyed gives, in its order, until f gives true:
    // whether it did. No list held: the statement's cursor walked.
    template <class F>
    bool each_keyed(std::string_view scope, std::string_view kind, F&& f) {
        return keyed_of(*c_, scope, kind, f, at_);
    }
    // A work allocator: the scratch's next arena, the last one's blocks
    // given back, else the view's allocator.
    [[nodiscard]] Chars work() { return scratch_ != nullptr ? Chars((*scratch_)()) : allocator_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return static_cast<std::uint64_t>(held_.size); }
    [[nodiscard]] std::uint64_t count() const noexcept { return static_cast<std::uint64_t>(held_.count); }

private:
    Connection* c_;
    Chars allocator_;
    ScratchView* scratch_;
    WhereView at_;
    Totals held_;
};

// A row an append or a forget inserts: its header's columns, its line and
// its body record or none.
struct RowView {
    std::string_view id, scope, lane, kind;
    std::optional<std::string_view> parent, target;
    std::int64_t seq = 0;
    std::string_view line;
    std::optional<std::string_view> record;
    bool body = false;  // its body key, the keys'
};

// Step 3: the lane's head against the expected one, else HeadConflict.
std::optional<Head> expect(Connection& c, std::string_view scope, std::string_view lane, Expected expected,
                           const WhereView& at);
// forget's guard: every head held as the guard expects it, and every
// guard lane held; else HeadConflict naming the lane.
void guard(Connection& c, std::span<const GuardView> guard, const WhereView& at);
// forget's erasure: each target's body row deleted, once, in order, where
// its entry is of scope, its id into erased; the bytes dropped, each
// record with its LF.
std::uint64_t erase(Connection& c, std::span<const Id> targets, std::string_view scope, Vector<Id>& erased,
                    const WhereView& at);
// Step 5: the entry rows in order, then the body rows; an insert's
// constraint on (scope, lane, seq) or id HeadConflict, the head read again.
void insert(Connection& c, std::span<const RowView> rows, Expected expected, const WhereView& at);
// Step 5: the head moved from the expected one, one row, or a lane's first
// inserted; then meta's size, count and forgets added to.
void advance(Connection& c, std::string_view scope, std::string_view lane, Expected expected, const Head& head,
             const WhereView& at);
void add_meta(Connection& c, std::int64_t size, std::int64_t count, std::int64_t forgets, const WhereView& at);
// A purge's raise of a meta row to at least value, in its own statement,
// composed into out.
[[nodiscard]] std::string_view raise_meta(std::string_view name, std::int64_t value, std::array<char, 96>& out) noexcept;
// size's figures: page_size, page_count, freelist_count, the files'.
void figures(Connection& c, Sizes& out, const WhereView& at);

// The reads (sqlite_read.cpp), through the wrapper. find, a view, a scan and
// an export give the bytes and keys held, parsing none.
[[nodiscard]] std::optional<Head> head_of(Connection& c, std::string_view scope, std::string_view lane,
                                          const WhereView& at);
// heads' read, in one read transaction: the head rows of scopes (none:
// every one; an entry ending in `*` by prefix) into rows in (scope, lane)
// order, their forget entries, and whether meta's purged is below forgets.
struct HeadsRead {
    std::uint64_t forgets = 0;
    bool pending = false;
};
[[nodiscard]] HeadsRead heads(Connection& c, const Scopes& scopes, Vector<LaneHead>& rows, const WhereView& at);
// An entry by id: its line's bytes into line and its record's into record, each where given, on its allocator; body
// where it has a body row. A record asked of a body key with no row and no forget is StoreCorrupt incoherent.
enum class Found : std::uint8_t { absent, entry, body };
[[nodiscard]] Found find(Connection& c, std::string_view id, memory::String* line, memory::String* record,
                         const WhereView& at);
// Whether a forget targets id; none is id, NotFound.
[[nodiscard]] bool forgotten(Connection& c, std::string_view id, const WhereView& at);

// A scan: a cursor at the last (scope, lane, seq) it gave, each next() one
// query for the first entry of its range past it, its keys' columns and
// line copied into out, nothing parsed. Made, it reads the greatest
// rowid of entry, and gives no row past it: entry rows are never deleted,
// each takes the greatest rowid plus one, and VACUUM keeps the rowids of a
// table with an index. So every entry committed before it began comes
// once, in order, and none after: each lane ends at its length at the
// scan's start. It borrows its store object's gate, connection, pages and
// path: used while that object lives; once it closes or moves, the gate
// raises StoreError closed.
class ScanView {
public:
    ScanView(const Gate& gate, Connection& c, std::span<std::byte> pages, Countdown* countdown,
             std::string_view path, const RangeView& range);
    ScanView(const ScanView&) = delete;
    ScanView& operator=(const ScanView&) = delete;
    ScanView(ScanView&&) noexcept = default;
    ScanView& operator=(ScanView&&) noexcept = default;
    ~ScanView() = default;

    // The next record into out, on out's allocator; false at the end.
    bool next(Record& out) {
        if (!fetch(out)) {
            return false;
        }
        past(out);
        return true;
    }
    // The next record lent to step: step(record, chars) inside the next,
    // the record and chars on a fresh arena over the pages, reset after the
    // step; false at the end, step not called. A step's throw leaves the
    // scan at that record.
    template <class Step>
        requires std::invocable<Step&, const Record&, const Chars&>
    bool next(Step&& step) {
        memory::Arena arena(pages_);
#if defined(QIVEN_FAULTS)
        if (countdown_ != nullptr) {
            arena.attach(countdown_);
        }
#endif
        const Chars chars(arena);
        Record r(chars);
        if (!fetch(r)) {
            return false;
        }
        step(std::as_const(r), chars);
        past(r);
        peak_ = std::max(peak_, arena.peak());
        return true;
    }
    // The counted peak of its greatest lent next: a plain next writes on
    // its caller's allocator alone.
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }

private:
    // The next record into out, the cursor where it was; false at the end.
    bool fetch(Record& out);
    // The cursor past r.
    void past(const Record& r) noexcept {
        at_scope_ = r.scope;
        at_lane_ = r.lane;
        at_seq_ = r.seq;
    }

    const Gate* gate_;
    Connection* c_;
    std::span<std::byte> pages_;
    [[maybe_unused]] Countdown* countdown_;
    std::string_view path_;
    std::optional<memory::Inline<scope_bytes>> scope_;
    std::optional<memory::Inline<lane_bytes>> lane_;
    std::int64_t after_;
    memory::Inline<scope_bytes> at_scope_;
    memory::Inline<lane_bytes> at_lane_;
    std::int64_t at_seq_ = std::numeric_limits<std::int64_t>::min();
    std::int64_t last_ = 0;  // the greatest rowid of entry at the scan's start
    bool done_ = false;
    std::size_t peak_ = 0;
};

#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Opened) == 56, "open's figures are pinned: seven counts");
#endif

}  // namespace qiven3::store::sqlite
