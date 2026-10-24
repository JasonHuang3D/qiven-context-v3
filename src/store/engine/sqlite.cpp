// sqlite: the engine's calls, all of them: the engine heap's allocator
// and its installation with the page cache's slots, Connection and
// Statement, and the mapping of every failing call by its extended code
// (sqlite.hpp).

#include "store/engine/sqlite.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "memory/pool.hpp"
#include "sqlite3.h"
#include "store/codes.hpp"
#include "store/errors.hpp"

namespace qiven3::store::sqlite {

namespace {

// The installed heap, which the engine's allocator reaches: the engine's
// allocator takes no argument, so the heap is the process's one.
std::atomic<memory::Heap*>& installed() noexcept {
    static std::atomic<memory::Heap*> heap{nullptr};
    return heap;
}

// The last request the heap refused, for MemoryError's detail.
std::atomic<std::size_t>& refused() noexcept {
    static std::atomic<std::size_t> request{0};
    return request;
}

memory::Heap& heap_or_fail() noexcept {
    memory::Heap* const heap = installed().load(std::memory_order_acquire);
    if (heap == nullptr) {
        memory::fail_fast("store/sqlite: the engine called its allocator with no heap installed");
    }
    return *heap;
}

void* engine_malloc(int n) noexcept {
    if (n < 0) {
        return nullptr;
    }
    void* const p = heap_or_fail().allocate(static_cast<std::size_t>(n));
    if (p == nullptr) {
        refused().store(static_cast<std::size_t>(n), std::memory_order_relaxed);
    }
    return p;
}

void engine_free(void* p) noexcept { heap_or_fail().free(p); }

void* engine_realloc(void* p, int n) noexcept {
    if (n < 0) {
        return nullptr;
    }
    void* const q = heap_or_fail().resize(p, static_cast<std::size_t>(n));
    if (q == nullptr) {
        refused().store(static_cast<std::size_t>(n), std::memory_order_relaxed);
    }
    return q;
}

int engine_size(void* p) noexcept {
    const std::size_t n = heap_or_fail().size(p);
    return n > static_cast<std::size_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                                         : static_cast<int>(n);
}

// The heap's class for n: 16 bytes doubled until it holds n.
int engine_roundup(int n) noexcept {
    int r = 16;
    while (r < n && r <= std::numeric_limits<int>::max() / 2) {
        r *= 2;
    }
    return r < n ? n : r;
}

int engine_init(void* /*data*/) noexcept { return SQLITE_OK; }

// A slot's bytes: a page and the engine's per-page header, at the
// multiple of 8 the engine takes; 0 when the engine refuses to give its
// header, initialized already.
std::size_t slot_bytes() noexcept {
    int header = 0;
    if (sqlite3_config(SQLITE_CONFIG_PCACHE_HDRSZ, &header) != SQLITE_OK || header <= 0) {
        return 0;
    }
    return ((page_size + static_cast<std::size_t>(header) + 7) / 8) * 8;
}

// The slots for connections and the engine's reserve; 0 when the pages cannot hold their bytes or
// their count or size passes the engine's int.
std::size_t slot_count(std::span<std::byte> pages, std::size_t connections, std::size_t size) noexcept {
    constexpr auto most = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const std::optional<std::size_t> each = memory::mul(cache_slots, connections);
    const std::optional<std::size_t> n = each ? memory::add(*each, slots_reserve) : std::nullopt;
    if (!n || size == 0 || size > most || *n > most) {
        return 0;
    }
    const std::optional<std::size_t> bytes = memory::mul(*n, size);
    return bytes && *bytes <= pages.size() ? *n : 0;
}

// The engine's page cache given back to the engine's own: no slot left
// pointing into pages past their engine heap.
void release_slots() noexcept {
    static_cast<void>(sqlite3_config(SQLITE_CONFIG_PAGECACHE, static_cast<void*>(nullptr), 0, 0));
}

// One of the engine's counts, now and its mark; the mark set to now when
// reset.
std::pair<std::size_t, std::size_t> status(int op, bool reset) noexcept {
    sqlite3_int64 now = 0;
    sqlite3_int64 mark = 0;
    if (sqlite3_status64(op, &now, &mark, reset ? 1 : 0) != SQLITE_OK || now < 0 || mark < 0) {
        return {0, 0};
    }
    return {static_cast<std::size_t>(now), static_cast<std::size_t>(mark)};
}

void engine_shutdown(void* /*data*/) noexcept {}

// The code a failing call left, extended where the connection holds the
// same primary code.
int extended(sqlite3* db, int rc) noexcept {
    if (db == nullptr) {
        return rc;
    }
    const int code = sqlite3_extended_errcode(db);
    return (code & 0xFF) == (rc & 0xFF) ? code : rc;
}

[[noreturn]] void raise_at(sqlite3* db, int rc, const WhereView& at) {
    const int code = extended(db, rc);
    if ((code & 0xFF) == SQLITE_NOMEM || code == SQLITE_IOERR_NOMEM) {
        const memory::Heap* const heap = installed().load(std::memory_order_acquire);
        throw memory::MemoryError::budget("store/sqlite", heap != nullptr ? heap->budget() : 0,
                                          refused().load(std::memory_order_relaxed),
                                          heap != nullptr ? heap->used() : 0);
    }
    throw to_error(code, at);
}

#if defined(QIVEN3_FAULTS)
std::atomic<Countdown*>& faults() noexcept {
    static std::atomic<Countdown*> countdown{nullptr};
    return countdown;
}

std::atomic<Countdown*>& arenas() noexcept {
    static std::atomic<Countdown*> countdown{nullptr};
    return countdown;
}
#endif

// Whether the engine calls' countdown fails this call: its result then
// SQLITE_IOERR, the engine not called. Never without QIVEN3_FAULTS.
bool injected() noexcept {
#if defined(QIVEN3_FAULTS)
    Countdown* const countdown = faults().load(std::memory_order_acquire);
    return countdown != nullptr && countdown->fail();
#else
    return false;
#endif
}

// A statement past the engine's int-sized length is refused, internal.
void fits(std::string_view text, const WhereView& at) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw StoreError(Code::internal, at.operation, at.path, "a statement past the engine's length");
    }
}

// A column's bytes, text read as its bytes: the engine converts a number
// to its text first.
std::string_view bytes_of(sqlite3_stmt* statement, int column) noexcept {
    const void* const p = sqlite3_column_blob(statement, column);
    const int n = sqlite3_column_bytes(statement, column);
    if (p == nullptr || n <= 0) {
        return {};
    }
    return {static_cast<const char*>(p), static_cast<std::size_t>(n)};
}

}  // namespace

int engine_version() noexcept { return sqlite3_libversion_number(); }

#if defined(QIVEN3_FAULTS)
void attach_calls(Countdown* calls, Countdown* arenas_too) noexcept {
    faults().store(calls, std::memory_order_release);
    arenas().store(arenas_too, std::memory_order_release);
}

Countdown* arenas_countdown() noexcept { return arenas().load(std::memory_order_acquire); }
#endif

bool engine_ready() noexcept { return installed().load(std::memory_order_acquire) != nullptr; }

bool remove_file(std::string_view path, std::string_view suffix) noexcept {
    if (injected()) {
        return false;
    }
    sqlite3_vfs* const vfs = sqlite3_vfs_find(nullptr);
    const std::size_t n = path.size() + suffix.size();
    if (vfs == nullptr || vfs->xDelete == nullptr || n < path.size() ||
        n >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    // The name on the engine's heap, ended by a NUL as the layer reads it.
    void* const block = sqlite3_malloc64(n + 1);
    if (block == nullptr) {
        return false;
    }
    const std::span<char> name(static_cast<char*>(block), n + 1);
    const auto end = std::copy(path.begin(), path.end(), name.begin());
    *std::copy(suffix.begin(), suffix.end(), end) = '\0';
    const int rc = vfs->xDelete(vfs, name.data(), 0);
    sqlite3_free(block);
    return rc == SQLITE_OK || rc == SQLITE_IOERR_DELETE_NOENT;
}

EngineHeap::EngineHeap(std::span<std::byte> pages, std::size_t connections)
    : size_(slot_bytes()), count_(slot_count(pages, connections, size_)),
      heap_(pages.first(pages.size() - (size_ * count_))),
      slots_(pages.subspan(pages.size() - (size_ * count_)).data()) {
    memory::Heap* none = nullptr;
    if (!installed().compare_exchange_strong(none, &heap_, std::memory_order_acq_rel)) {
        throw StoreError(Code::internal, open_name, {}, "an engine heap is installed already");
    }
    if (count_ == 0) {
        installed().store(nullptr, std::memory_order_release);
        throw StoreError(Code::internal, open_name, {}, "the engine heap's pages cannot hold the page cache's slots");
    }
    // The engine copies the methods before the call returns.
    sqlite3_mem_methods methods{&engine_malloc, &engine_free,  &engine_realloc,  &engine_size,
                                &engine_roundup, &engine_init, &engine_shutdown, nullptr};
    int rc = sqlite3_config(SQLITE_CONFIG_MALLOC, &methods);
    if (rc == SQLITE_OK) {
        void* const first = slots_;  // the engine reads a void pointer
        rc = injected() ? SQLITE_IOERR
                        : sqlite3_config(SQLITE_CONFIG_PAGECACHE, first, static_cast<int>(size_),
                                         static_cast<int>(count_));
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_initialize();
    }
    if (rc != SQLITE_OK) {
        static_cast<void>(sqlite3_shutdown());
        release_slots();
        installed().store(nullptr, std::memory_order_release);
        std::array<char, result_detail_bytes> buffer{};
        throw StoreError(Code::internal, open_name, {}, result_detail(rc, buffer));
    }
    // The marks from this engine heap on.
    static_cast<void>(status(SQLITE_STATUS_PAGECACHE_USED, true));
    static_cast<void>(status(SQLITE_STATUS_PAGECACHE_OVERFLOW, true));
}

EngineHeap::~EngineHeap() {
    static_cast<void>(sqlite3_shutdown());
    release_slots();
    installed().store(nullptr, std::memory_order_release);
    if (heap_.used() != 0) {
        memory::fail_fast("store/sqlite: the engine heap holds blocks past the engine's end");
    }
}

Slots EngineHeap::slots() const noexcept {
    const auto [used, peak] = status(SQLITE_STATUS_PAGECACHE_USED, false);
    const auto [overflow, overflow_peak] = status(SQLITE_STATUS_PAGECACHE_OVERFLOW, false);
    return {size_, count_, used, peak, overflow, overflow_peak};
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), statement_(other.statement_) {
    other.db_ = nullptr;
    other.statement_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        finalize();
        db_ = other.db_;
        statement_ = other.statement_;
        other.db_ = nullptr;
        other.statement_ = nullptr;
    }
    return *this;
}

Statement::~Statement() { finalize(); }

void Statement::finalize() noexcept {
    if (statement_ != nullptr) {
        static_cast<void>(sqlite3_finalize(statement_));
        statement_ = nullptr;
    }
}

void Statement::bind(int index, std::int64_t value, const WhereView& at) {
    const int rc = injected() ? SQLITE_IOERR : sqlite3_bind_int64(statement_, index, value);
    if (rc != SQLITE_OK) {
        raise_at(db_, rc, at);
    }
}

void Statement::bind_text(int index, std::string_view text, const WhereView& at) {
    // An empty view may have no data; the empty text is bound, never null.
    const char* const data = text.empty() ? "" : text.data();
    const int rc =
        injected() ? SQLITE_IOERR : sqlite3_bind_text64(statement_, index, data, text.size(), nullptr, SQLITE_UTF8);
    if (rc != SQLITE_OK) {
        raise_at(db_, rc, at);
    }
}

void Statement::bind_blob(int index, std::string_view bytes, const WhereView& at) {
    const int rc = injected()      ? SQLITE_IOERR
                   : bytes.empty() ? sqlite3_bind_zeroblob(statement_, index, 0)
                                   : sqlite3_bind_blob64(statement_, index, bytes.data(), bytes.size(), nullptr);
    if (rc != SQLITE_OK) {
        raise_at(db_, rc, at);
    }
}

void Statement::bind_null(int index, const WhereView& at) {
    const int rc = injected() ? SQLITE_IOERR : sqlite3_bind_null(statement_, index);
    if (rc != SQLITE_OK) {
        raise_at(db_, rc, at);
    }
}

bool Statement::step(const WhereView& at) {
    const int rc = injected() ? SQLITE_IOERR : sqlite3_step(statement_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    raise_at(db_, rc, at);
}

int Statement::step_code() noexcept {
    const int rc = injected() ? SQLITE_IOERR : sqlite3_step(statement_);
    return rc == SQLITE_ROW || rc == SQLITE_DONE ? rc : extended(db_, rc);
}

void Statement::reset() noexcept {
    static_cast<void>(sqlite3_reset(statement_));
    static_cast<void>(sqlite3_clear_bindings(statement_));
}

std::int64_t Statement::integer(int column) const noexcept { return sqlite3_column_int64(statement_, column); }

std::string_view Statement::text(int column) const noexcept { return bytes_of(statement_, column); }

std::string_view Statement::blob(int column) const noexcept { return bytes_of(statement_, column); }

bool Statement::null(int column) const noexcept { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }

Connection Connection::open(const memory::String& path, bool read_only, const WhereView& at) {
    if (!engine_ready()) {
        throw StoreError(Code::internal, at.operation, at.path, "no engine heap installed");
    }
    sqlite3* db = nullptr;
    const int flags = (read_only ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE) | SQLITE_OPEN_EXRESCODE;
    const int rc = injected() ? SQLITE_IOERR : sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
    // A refused open may still give a handle, to be closed.
    Connection c(db);
    if (rc != SQLITE_OK) {
        raise_at(db, rc, at);
    }
    static_cast<void>(sqlite3_extended_result_codes(db, 1));
    c.wait(busy_milliseconds);
    return c;
}

Connection::Connection(Connection&& other) noexcept : db_(other.db_) { other.db_ = nullptr; }

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

Connection::~Connection() { close(); }

Statement Connection::prepare(std::string_view sql, const WhereView& at) {
    fits(sql, at);
    sqlite3_stmt* statement = nullptr;
    const int rc = injected() ? SQLITE_IOERR
                              : sqlite3_prepare_v3(db_, sql.data(), static_cast<int>(sql.size()), 0, &statement, nullptr);
    Statement s(db_, statement);
    if (rc != SQLITE_OK) {
        raise_at(db_, rc, at);
    }
    if (statement == nullptr) {
        throw StoreError(Code::internal, at.operation, at.path, "an empty statement");
    }
    return s;
}

void Connection::exec(std::string_view sql, const WhereView& at) {
    Statement s = prepare(sql, at);
    while (s.step(at)) {
    }
}

std::int64_t Connection::integer(std::string_view sql, const WhereView& at) {
    Statement s = prepare(sql, at);
    if (!s.step(at)) {
        throw StoreError(Code::internal, at.operation, at.path, "a query with no row");
    }
    return s.integer(0);
}

void Connection::raise(int code, const WhereView& at) const { raise_at(db_, code, at); }

int Connection::run(std::string_view sql, std::span<std::int64_t> row) noexcept {
    if (db_ == nullptr || sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return SQLITE_MISUSE;
    }
    sqlite3_stmt* statement = nullptr;
    int rc = injected() ? SQLITE_IOERR
                        : sqlite3_prepare_v3(db_, sql.data(), static_cast<int>(sql.size()), 0, &statement, nullptr);
    if (rc == SQLITE_OK && statement != nullptr) {
        bool first = true;
        while ((rc = injected() ? SQLITE_IOERR : sqlite3_step(statement)) == SQLITE_ROW) {
            for (std::size_t i = 0; first && i < row.size(); ++i) {
                row[i] = sqlite3_column_int64(statement, static_cast<int>(i));
            }
            first = false;
        }
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
    }
    const int code = rc == SQLITE_OK ? rc : extended(db_, rc);
    static_cast<void>(sqlite3_finalize(statement));
    return code;
}

void Connection::wait(int milliseconds) noexcept {
    if (db_ != nullptr) {
        static_cast<void>(sqlite3_busy_timeout(db_, milliseconds));
    }
}

std::int64_t Connection::changes() const noexcept { return db_ != nullptr ? sqlite3_changes64(db_) : 0; }

bool Connection::in_transaction() const noexcept { return db_ != nullptr && sqlite3_get_autocommit(db_) == 0; }

void Connection::rollback() noexcept {
    if (!in_transaction()) {
        return;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v3(db_, "ROLLBACK", -1, 0, &statement, nullptr) == SQLITE_OK) {
        static_cast<void>(sqlite3_step(statement));
    }
    static_cast<void>(sqlite3_finalize(statement));
}

std::string_view Connection::filename() const noexcept {
    if (db_ == nullptr) {
        return {};
    }
    const char* const name = sqlite3_db_filename(db_, "main");
    return name == nullptr ? std::string_view() : std::string_view(name);
}

void Connection::close() noexcept {
    if (db_ != nullptr) {
        static_cast<void>(sqlite3_close_v2(db_));
        db_ = nullptr;
    }
}

}  // namespace qiven3::store::sqlite
