#pragma once

// connection: the SQLite wrapper, its engine heap and its typed error.
// The engine's header is included by src/sqlite.cpp alone, which holds every
// call into the engine: this header declares the engine's two handle types
// by name and nothing else of it, so that a module that runs statements
// through the wrapper neither includes the header nor names the engine
// (B1). sqlite.cpp sits in src/, whose analysis configuration lets the
// engine's variadic configuration call pass (B22). What a caller runs
// through the wrapper (its settings, its statements, its busy wait, the
// names of its operations) is the caller's.
//
// EngineHeap is the process's one engine heap: made over pages its maker
// takes, it routes every allocation of the engine to a qiven::memory::Heap
// (SQLITE_CONFIG_MALLOC), its page cache to slots at the back of the
// pages (SQLITE_CONFIG_PAGECACHE), then the engine's initialize, the build
// omitting the automatic one; its destructor shuts the engine down. One is
// installed at a time; open refuses with none.
//
// Connection owns one engine connection and Statement one prepared
// statement, each a move-only value closed or finalized by its destructor.
// Every call that fails is mapped by its extended code: NOMEM, the heap
// past its budget, MemoryError budget naming the engine heap
// (storage_sqlite); anything else EngineError engine, holding the code and
// the call's site, so that every error is typed. A call is given its site
// (Site): the operation and the path its caller names. The wrapper's own
// refusals are EngineError too, each with a refusal of its own.
//
// Allocations: on the engine heap alone (the engine's own, its cached
// pages on the slots at the back of its pages). thread: a Connection and
// its statements one; EngineHeap's heap shared, under its own lock, and
// its slots under the engine's.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "qiven/memory/memory.hpp"
#include "qiven/memory/pool.hpp"

// The engine's two handle types, by name alone.
struct sqlite3;
struct sqlite3_stmt;

namespace qiven::storage_sqlite {

// The engine heap's connections by default: its bytes are the engine's
// budget, a connection's, times these.
inline constexpr std::size_t default_connections = 8;
// The engine's page cache on slots of its own (the engine's page-cache
// setting), each a page of page_size and the engine's per-page header:
// cache_slots a connection, at the back of the engine heap's pages, the
// heap over the rest. A connection's full cache, as measured, 483 slots:
// its pages (2,000 KiB over a page and the engine's share of the header)
// and the connection's page buffers. Beside them the engine's reserve, below
// which a cache recycles its pages rather than grow: past the last free
// slots it recycles, or takes a page from the heap. Each is a default its
// maker may change (Cache).
inline constexpr std::size_t page_size = 4096;
inline constexpr std::size_t cache_slots = 483;
inline constexpr std::size_t slots_reserve = 10;

// The page cache an engine heap is made with: a page's bytes, the slots a
// connection, and the engine's reserve.
struct Cache {
    std::size_t page = page_size;
    std::size_t slots = cache_slots;
    std::size_t reserve = slots_reserve;
};

// When a setting is written: on every connection, and read back; or on a
// read-write one, a read-only one reading it back alone (journal_mode).
enum class When : std::uint8_t { every, read_write };

// A setting, its value as the engine reads it back, as text.
struct Setting {
    qiven::memory::Inline<24> name;
    qiven::memory::Inline<16> value;
    When when = When::every;
};

constexpr Setting setting(std::string_view name, std::string_view value, When when = When::every) noexcept {
    return Setting{qiven::memory::Inline<24>(name), qiven::memory::Inline<16>(value), when};
}

// A call's site, as its caller names it: the operation and the path.
struct Site {
    std::string_view operation;
    std::string_view path;
};

// What an EngineError refuses: a failing engine call (engine, its code the
// engine's extended code), or one of the wrapper's own, its code 0: no
// engine heap installed; a statement past the engine's length; an empty
// statement; a query with no row; an engine heap installed already; pages
// that cannot hold the page cache's slots; and the engine refusing its
// configuration, whose code is the engine's.
enum class Refusal : std::uint8_t { engine, no_heap, too_long, empty, no_row, installed, slots, configuration };

// Each text field's bytes: the operation; the path; the wrapper's detail.
inline constexpr std::size_t operation_bytes = 32;
inline constexpr std::size_t path_bytes = 512;
inline constexpr std::size_t detail_bytes = 64;

// The wrapper's typed error: its refusal, the engine's code, the call's
// site and the wrapper's own detail, each held in place, cut at a code
// point past its bytes. An engine heap's making names no site. A plain
// value, thrown by value; it allocates nothing.
class EngineError {
public:
    constexpr EngineError(Refusal refusal, int code, const Site& at, std::string_view detail = {}) noexcept
        : operation_(at.operation), path_(at.path), detail_(detail), code_(code), refusal_(refusal) {}

    [[nodiscard]] constexpr Refusal refusal() const noexcept { return refusal_; }
    [[nodiscard]] constexpr int code() const noexcept { return code_; }
    [[nodiscard]] constexpr std::string_view operation() const noexcept { return operation_.view(); }
    [[nodiscard]] constexpr std::string_view path() const noexcept { return path_.view(); }
    [[nodiscard]] constexpr std::string_view detail() const noexcept { return detail_.view(); }

private:
    qiven::memory::Inline<operation_bytes> operation_;
    qiven::memory::Inline<path_bytes> path_;
    qiven::memory::Inline<detail_bytes> detail_;
    int code_;
    Refusal refusal_;
};

// The engine's version number, as the library gives it.
[[nodiscard]] int engine_version() noexcept;
// Whether an engine heap is installed, so that open may connect.
[[nodiscard]] bool engine_ready() noexcept;
// Removes the file at path's UTF-8 bytes and suffix through the engine's
// default file layer (its delete, which removes files alone); true when it
// is gone or was never there. Under QIVEN_FAULTS it asks the engine
// calls' countdown first, a failed one false.
[[nodiscard]] bool remove_file(std::string_view path, std::string_view suffix) noexcept;

#if defined(QIVEN_FAULTS)
// The engine calls' countdown, the process's one: each engine call the
// wrapper maps asks it first, a failed one taking SQLITE_IOERR as its
// result, the engine not called. None, nullptr.
void attach_calls(qiven::memory::Countdown* calls) noexcept;
#endif

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
// page cache's slots for connections at their back, as cache gives them.
// Its making refuses, EngineError, while another is installed
// (installed), when the pages cannot hold the slots (slots) or when the
// engine refuses its configuration (configuration); its end shuts the
// engine down, every connection closed before it, takes the slots back
// from the engine's configuration, and fails fast if a block is still held
// then.
class EngineHeap {
public:
    explicit EngineHeap(std::span<std::byte> pages, std::size_t connections = default_connections,
                        const Cache& cache = Cache{});
    EngineHeap(const EngineHeap&) = delete;
    EngineHeap& operator=(const EngineHeap&) = delete;
    EngineHeap(EngineHeap&&) = delete;
    EngineHeap& operator=(EngineHeap&&) = delete;
    ~EngineHeap();

    [[nodiscard]] qiven::memory::Heap& heap() noexcept { return heap_; }
    [[nodiscard]] Slots slots() const noexcept;

private:
    std::size_t size_;   // a slot's bytes; 0 when the engine refused to give its header
    std::size_t count_;  // 0 when the pages cannot hold them
    qiven::memory::Heap heap_;  // over the pages before the slots
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
    void bind(int index, std::int64_t value, const Site& at);
    void bind_text(int index, std::string_view text, const Site& at);
    void bind_blob(int index, std::string_view bytes, const Site& at);
    void bind_null(int index, const Site& at);
    // true: a row; false: done; any other result mapped and thrown.
    bool step(const Site& at);
    // A step's own extended code, unmapped: 100 a row, 101 done, else the
    // failure a caller maps itself.
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
    // creating, with the caller's busy wait in milliseconds; an engine heap
    // must be installed (EngineError no_heap).
    static Connection open(const qiven::memory::String& path, bool read_only, int busy_milliseconds,
                           const Site& at);
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    ~Connection();

    // One statement; text past the engine's length is refused, EngineError
    // too_long, and an empty one empty.
    [[nodiscard]] Statement prepare(std::string_view sql, const Site& at);
    // Each statement of sql in turn, its rows read and dropped.
    void exec(std::string_view sql, const Site& at);
    // The first column of a query's first row; a query with none is
    // EngineError no_row.
    [[nodiscard]] std::int64_t integer(std::string_view sql, const Site& at);
    // The mapping of a failure's code at this connection.
    [[noreturn]] void raise(int code, const Site& at) const;
    // A statement that never throws: sql run to its end, its first row's
    // first columns into row; 0, or the failing extended code (NOMEM among
    // them).
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

#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Setting) == 64, "a setting's size is pinned: its name and value in place and when");
static_assert(sizeof(Statement) == 16, "a statement's size is pinned: two handles");
static_assert(sizeof(Connection) == 8, "a connection's size is pinned: its handle");
#endif

}  // namespace qiven::storage_sqlite
