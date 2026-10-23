#pragma once

// sqlite: the SQLite store's connection wrapper, its engine heap and its
// open steps. The engine's header is included by sqlite.cpp alone, which
// holds every call into the engine: this header declares the engine's two
// handle types by name and nothing else of it, so a module that runs
// statements through the wrapper (exchange) neither includes the header
// nor names the engine (B1). The two sit in engine/, the store's one
// directory where the engine is named, whose analysis configuration lets
// the engine's variadic configuration call pass (B22). In the store's own
// directory, sqlite_open.cpp holds the open steps, through the wrapper,
// sqlite_write.cpp the writes, sqlite_read.cpp the reads; sqlite_store.hpp
// the store itself, a template on the platform.
//
// EngineHeap is the process's one engine heap: made over pages its maker
// takes, it routes every allocation of the engine to a memory::Heap
// (SQLITE_CONFIG_MALLOC), its page cache to slots at the back of the
// pages (SQLITE_CONFIG_PAGECACHE), then the engine's initialize, the build
// omitting the automatic one; its destructor shuts the engine down. One is
// installed at a time; open refuses with none.
//
// Connection owns one engine connection and Statement one prepared
// statement, each a move-only value closed or finalized by its destructor.
// Every call that fails is mapped by its extended code: NOMEM, the heap
// past its budget, MemoryError budget naming the engine heap; anything
// else codes' to_error(code, where), so every error is typed. A call is
// given its site (WhereView): the operation, the store's path and what its
// mapping needs.
//
// Allocations: on the engine heap alone (the engine's own, its cached
// pages on the slots at the back of its pages), and the open steps'
// std::filesystem paths (presence, the checkout walk), which the standard
// library allocates. thread: a Connection and its statements one;
// EngineHeap's heap shared, under its own lock, and its slots under the
// engine's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "memory/pool.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

// The engine's two handle types, by name alone.
struct sqlite3;
struct sqlite3_stmt;

namespace qiven3::store::sqlite {

// The file's identity and the engine's least version (STRICT, 3.37.0).
inline constexpr std::int64_t application_id = 0x71763301;
inline constexpr std::int64_t user_version = 1;
inline constexpr int least_version = 3037000;
// The busy wait, a connection's: 5 s; a purge's statements set 0.
inline constexpr int busy_milliseconds = 5000;
inline constexpr double busy_seconds = 5.0;
// The operation open's errors name, and the public guard's.
inline constexpr std::string_view open_name = "open";
inline constexpr std::string_view guard_name = "guard";
// The engine heap's connections: its bytes are the engine's budget, a
// connection's, times these.
inline constexpr std::size_t default_connections = 8;
// The engine's page cache on slots of its own (the engine's page-cache
// setting), each a page of page_size and the engine's per-page header:
// cache_slots a connection, at the back of the engine heap's pages, the
// heap over the rest. A connection's full cache, as measured, 483 slots:
// its pages (2,000 KiB over a page and the engine's share of the header)
// and the connection's page buffers. Beside them the engine's reserve, below
// which a cache recycles its pages rather than grow: past the last free
// slots it recycles, or takes a page from the heap.
inline constexpr std::size_t page_size = 4096;
inline constexpr std::size_t cache_slots = 483;
inline constexpr std::size_t slots_reserve = 10;

// A call's site: the operation and the store's path, with the busy wait's
// seconds for StoreBusy.
constexpr WhereView site(std::string_view operation, std::string_view path, double seconds = busy_seconds) noexcept {
    WhereView at;
    at.operation = operation;
    at.path = path;
    at.seconds = seconds;
    return at;
}

// When a setting is written: on every connection, and read back; on a
// read-write one, a read-only one reading it back alone (journal_mode); or
// at create, inside its transaction, and read at identify.
enum class When : std::uint8_t { every, read_write, create };

// A setting, its value as the engine reads it back, as text.
struct Setting {
    memory::Inline<16> name;
    memory::Inline<16> value;
    When when = When::every;
};

constexpr Setting setting(std::string_view name, std::string_view value, When when = When::every) noexcept {
    return Setting{memory::Inline<16>(name), memory::Inline<16>(value), when};
}

// SETTINGS, the table the store applies and a test compares with the
// text's: journal_mode first, so that the rest apply in WAL; busy_timeout
// as the wrapper sets it; the identity last.
inline constexpr std::array<Setting, 11> settings = {
    setting("journal_mode", "wal", When::read_write),
    setting("synchronous", "2"),
    setting("secure_delete", "1"),
    setting("temp_store", "2"),
    setting("foreign_keys", "1"),
    setting("trusted_schema", "0"),
    setting("cell_size_check", "1"),
    setting("auto_vacuum", "0"),
    setting("busy_timeout", "5000"),
    setting("application_id", "1903571713", When::create),
    setting("user_version", "1", When::create),
};

// The engine's version number, as the library gives it.
[[nodiscard]] int engine_version() noexcept;
// Whether an engine heap is installed, so that open may connect.
[[nodiscard]] bool engine_ready() noexcept;
// Removes the file at path's UTF-8 bytes and suffix through the engine's
// default file layer (its delete, which removes files alone); true when it
// is gone or was never there. Under QIVEN3_FAULTS it asks the engine
// calls' countdown first, a failed one false.
[[nodiscard]] bool remove_file(std::string_view path, std::string_view suffix) noexcept;

// The page cache's slots: a slot's bytes and their count; those held now
// and their mark since the engine heap was made; and the heap's bytes of
// pages taken past the slots, now and their mark.
struct Slots {
    std::size_t size = 0;
    std::size_t count = 0;
    std::size_t used = 0;
    std::size_t peak = 0;
    std::size_t overflow = 0;
    std::size_t overflow_peak = 0;
};

// The process's engine heap over pages its maker keeps past it, the
// page cache's slots for connections at their back. Its making refuses,
// StoreError internal, while another is installed, when the pages cannot
// hold the slots or when the engine refuses its configuration; its end
// shuts the engine down, every connection closed before it, takes the
// slots back from the engine's configuration, and fails fast if a block is
// still held then.
class EngineHeap {
public:
    EngineHeap(std::span<std::byte> pages, std::size_t connections);
    EngineHeap(const EngineHeap&) = delete;
    EngineHeap& operator=(const EngineHeap&) = delete;
    EngineHeap(EngineHeap&&) = delete;
    EngineHeap& operator=(EngineHeap&&) = delete;
    ~EngineHeap();

    [[nodiscard]] memory::Heap& heap() noexcept { return heap_; }
    [[nodiscard]] Slots slots() const noexcept;

private:
    std::size_t size_;   // a slot's bytes; 0 when the engine refused to give its header
    std::size_t count_;  // 0 when the pages cannot hold them
    memory::Heap heap_;  // over the pages before the slots
    std::byte* slots_;   // the first slot
};

class Connection;

// A prepared statement. Bound views are lent until the statement's next
// reset or its end. A column's view lasts until the next step or reset.
class Statement {
public:
    Statement() noexcept = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    ~Statement();

    // Parameters from 1.
    void bind(int index, std::int64_t value, const WhereView& at);
    void bind_text(int index, std::string_view text, const WhereView& at);
    void bind_blob(int index, std::string_view bytes, const WhereView& at);
    void bind_null(int index, const WhereView& at);
    // true: a row; false: done; any other result mapped and thrown.
    bool step(const WhereView& at);
    // A step's own extended code, unmapped: 100 a row, 101 done, else the
    // failure a caller maps itself (an insert's HeadConflict, its actual
    // head read again first).
    [[nodiscard]] int step_code() noexcept;
    // Reset, and its bindings cleared.
    void reset() noexcept;
    // Columns from 0.
    [[nodiscard]] std::int64_t integer(int column) const noexcept;
    [[nodiscard]] std::string_view text(int column) const noexcept;
    [[nodiscard]] std::string_view blob(int column) const noexcept;
    [[nodiscard]] bool null(int column) const noexcept;

private:
    friend class Connection;
    Statement(sqlite3* db, sqlite3_stmt* statement) noexcept : db_(db), statement_(statement) {}
    void finalize() noexcept;

    sqlite3* db_ = nullptr;               // the connection's, which outlives the statement
    sqlite3_stmt* statement_ = nullptr;   // owned
};

// One connection, autocommit until a BEGIN its caller runs.
class Connection {
public:
    Connection() noexcept = default;
    // Opens path, which must exist, read-write or read-only, never
    // creating, with the 5 s busy wait; an engine heap must be installed.
    static Connection open(const memory::String& path, bool read_only, const WhereView& at);
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    ~Connection();

    // One statement; text past it is refused, StoreError internal.
    [[nodiscard]] Statement prepare(std::string_view sql, const WhereView& at);
    // Each statement of sql in turn, its rows read and dropped.
    void exec(std::string_view sql, const WhereView& at);
    // The first column of a query's first row; a query with none is
    // StoreError internal.
    [[nodiscard]] std::int64_t integer(std::string_view sql, const WhereView& at);
    // The mapping of a failure's code at this connection.
    [[noreturn]] void raise(int code, const WhereView& at) const;
    // A purge's statement, which never throws: sql run to its end, its
    // first row's first columns into row; 0, or the failing extended code
    // (NOMEM among them).
    [[nodiscard]] int run(std::string_view sql, std::span<std::int64_t> row = {}) noexcept;
    // The busy wait from now on, in milliseconds.
    void wait(int milliseconds) noexcept;
    // The rows the last INSERT, UPDATE or DELETE changed (an UPDATE that
    // matches none raises nothing).
    [[nodiscard]] std::int64_t changes() const noexcept;
    // Whether a transaction is open.
    [[nodiscard]] bool in_transaction() const noexcept;
    // ROLLBACK when a transaction is open, a failure ignored.
    void rollback() noexcept;
    // The main database's path as the engine holds it; empty once closed.
    [[nodiscard]] std::string_view filename() const noexcept;
    [[nodiscard]] bool is_open() const noexcept { return db_ != nullptr; }
    // Closes now; a statement left open defers the close to its end.
    void close() noexcept;

private:
    explicit Connection(sqlite3* db) noexcept : db_(db) {}

    sqlite3* db_ = nullptr;  // owned
};

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
// A refused create's files removed: its -journal, -wal and -shm, each
// tried, then the file once none of them is left, so that no later file
// at path meets them; true when none is left.
[[nodiscard]] bool unmake(std::string_view path) noexcept;
// Steps 2 (the connection) to 7 on a file presence() passed, or that
// create made. Under create, a step refused closes the connection, unmakes
// the files and raises the step's error, whether or not each went.
[[nodiscard]] Connection connect(const memory::String& path, bool create, bool read_only, const WhereView& at,
                                 Opened& opened);

// A file's size in bytes, the path's UTF-8 bytes and a suffix after them
// ("-wal", "-shm"); none when absent, not a regular file or unreadable
// (sizes/sizes.cpp).
[[nodiscard]] std::optional<std::uint64_t> file_bytes(std::string_view path, std::string_view suffix = {});

// The writes (sqlite_write.cpp), through the wrapper.
using Chars = memory::Allocator<char>;
#if defined(QIVEN3_FAULTS)
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
// last one's blocks all given back; under QIVEN3_FAULTS each asks the
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

// Part 2's view of the transaction (stage::View): the head, a header and
// a body by id, the forgotten set, F138's clearings and held contents, the
// size and the count the transaction began with. A clearing works on the
// scratch's fresh arena, one direction record at a time, else on the
// view's allocator.
class WriteView {
public:
    WriteView(Connection& c, const Chars& allocator, ScratchView* scratch, const WhereView& at,
              const Totals& held) noexcept
        : c_(&c), allocator_(allocator), scratch_(scratch), at_(at), held_(held) {}

    [[nodiscard]] std::optional<Head> head(std::string_view scope, std::string_view lane);
    bool header(std::string_view id, Header& out);
    bool body(std::string_view id, entry::validator::Text<Chars>& out);
    [[nodiscard]] bool forgotten(std::string_view id);
    [[nodiscard]] bool cleared(std::string_view scope, std::string_view content, std::string_view pattern,
                               std::string_view at);
    [[nodiscard]] bool holds_content(std::string_view content);
    [[nodiscard]] std::uint64_t size() const noexcept { return static_cast<std::uint64_t>(held_.size); }
    [[nodiscard]] std::uint64_t count() const noexcept { return static_cast<std::uint64_t>(held_.count); }

private:
    bool clears(const Id& root, std::string_view scope, std::string_view content, std::string_view pattern,
                std::string_view at);

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
};

// Step 3: the lane's head against the expected one, else HeadConflict.
std::optional<Head> expect(Connection& c, std::string_view scope, std::string_view lane, Expected expected,
                           const WhereView& at);
// forget's guard: every head held as the guard expects it, and every
// guard lane held; else HeadConflict naming the lane.
void guard(Connection& c, std::span<const GuardView> guard, const WhereView& at);
// forget's whole records: a live revision (not forgotten, of a kind but
// the change kinds) of a target, not itself a target, StoreError
// whole_record naming the first forget of that target, "entry <j>".
void whole_record(Connection& c, std::span<const Id> targets, const WhereView& at);
// forget's erasure: each target's body row deleted, once, in order, its id
// into erased; the bytes dropped, each record with its LF.
std::uint64_t erase(Connection& c, std::span<const Id> targets, Vector<Id>& erased, const WhereView& at);
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

// The reads (sqlite_read.cpp), through the wrapper. A header line is
// parsed and checked, its id recomputed and compared with the row's (a
// mismatch StoreCorrupt id_mismatch); find and a scan compare each column
// beside the line with its field too (StoreCorrupt incoherent).
[[nodiscard]] Header read_header(std::string_view id, std::string_view line, const Chars& allocator,
                                 const WhereView& at);
// A row of id, scope, lane, seq, parent, kind, target and line, in this
// order (find's, a scan's, an export's): its line read so, then each column.
[[nodiscard]] Header row_header(const Statement& s, const Chars& allocator, const WhereView& at);
[[nodiscard]] std::optional<Head> head_of(Connection& c, std::string_view scope, std::string_view lane,
                                          const WhereView& at);
// h's fields made again on out's allocator.
void copy_header(const Header& h, Header& out);
// heads' read, in one read transaction: the head rows of scopes (none:
// every one; `project:*` every project scope) into rows in (scope, lane)
// order, their forget entries, and whether meta's purged is below forgets.
struct HeadsRead {
    std::uint64_t forgets = 0;
    bool pending = false;
};
[[nodiscard]] HeadsRead heads(Connection& c, const Scopes& scopes, Vector<LaneHead>& rows, const WhereView& at);
// An entry by id, its header checked into out on out's allocator. With
// record given: no body row, nothing parsed; else its record into it.
enum class Found : std::uint8_t { absent, entry, body };
[[nodiscard]] Found find(Connection& c, std::string_view id, Header& out, memory::String* record,
                         const WhereView& at);
// Whether a forget targets id; none is id, NotFound.
[[nodiscard]] bool forgotten(Connection& c, std::string_view id, const WhereView& at);

// A scan: a cursor at the last (scope, lane, seq) it gave, each next() one
// query for the first entry of its range past it, checked as find's, on a
// fresh arena over the pages, copied into out. Made, it reads the greatest
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

    bool next(Header& out);
    // The counted peak of its greatest next.
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }

private:
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
static_assert(sizeof(Setting) == 56, "a setting's size is pinned: two names in place and when");
static_assert(sizeof(Statement) == 16, "a statement's size is pinned: two handles");
static_assert(sizeof(Connection) == 8, "a connection's size is pinned: its handle");
static_assert(sizeof(Opened) == 56, "open's figures are pinned: seven counts");
#endif

}  // namespace qiven3::store::sqlite
