#pragma once

// sqlite: the SQLite store's connection wrapper, its engine heap and its
// open steps. The engine's header is included by sqlite.cpp alone, which
// holds every call into the engine: this header declares the engine's two
// handle types by name and nothing else of it, so a module that runs
// statements through the wrapper (exchange) neither includes the header
// nor names the engine (B1). sqlite_open.cpp holds the open steps, through
// the wrapper; sqlite_store.hpp the store itself, a template on the
// platform.
//
// EngineHeap is the process's one engine heap: made over pages its maker
// takes, it routes every allocation of the engine to a memory::Heap
// (SQLITE_CONFIG_MALLOC, then the engine's initialize, the build omitting
// the automatic one), and its destructor shuts the engine down. One is
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
// Allocations: on the engine heap alone (the engine's own), and the
// open steps' std::filesystem paths (presence, the checkout walk), which
// the standard library allocates. thread: a Connection and its statements
// one; EngineHeap's heap shared, under its own lock.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

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

// The process's engine heap over pages its maker keeps past it. Its
// making refuses, StoreError internal, while another is installed or when
// the engine refuses its configuration; its end shuts the engine down,
// every connection closed before it, and fails fast if a block is still
// held then.
class EngineHeap {
public:
    explicit EngineHeap(std::span<std::byte> pages);
    EngineHeap(const EngineHeap&) = delete;
    EngineHeap& operator=(const EngineHeap&) = delete;
    EngineHeap(EngineHeap&&) = delete;
    EngineHeap& operator=(EngineHeap&&) = delete;
    ~EngineHeap();

    [[nodiscard]] memory::Heap& heap() noexcept { return heap_; }

private:
    memory::Heap heap_;
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
// Steps 2 (the connection) to 7 on a file presence() passed, or that
// create made.
[[nodiscard]] Connection connect(const memory::String& path, bool create, bool read_only, const WhereView& at,
                                 Opened& opened);

#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Setting) == 56, "a setting's size is pinned: two names in place and when");
static_assert(sizeof(Statement) == 16, "a statement's size is pinned: two handles");
static_assert(sizeof(Connection) == 8, "a connection's size is pinned: its handle");
static_assert(sizeof(Opened) == 56, "open's figures are pinned: seven counts");
#endif

}  // namespace qiven3::store::sqlite
