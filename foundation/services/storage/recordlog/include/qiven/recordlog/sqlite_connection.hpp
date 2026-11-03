#pragma once

// sqlite_connection: the store's connection, statement and engine heap,
// each a thin value over the wrapper's own (storage_sqlite's
// connection.hpp), so that the store's calls take the store's site
// (WhereView) and raise the store's errors. Each method calls the wrapper
// with the site's operation and path, and maps an EngineError it throws in
// one place (sqlite_connection.cpp): a failing engine call by codes'
// to_error(code, where); the engine refusing its configuration StoreError
// internal, the code's name and number its detail; any other refusal of
// the wrapper's StoreError internal, the wrapper's detail its detail. The
// engine's NOMEM passes as the wrapper raises it, MemoryError budget. The
// engine heap's refusals name open_name and no path. The wrapper's values
// the store's code names (Setting, When, setting, engine_version,
// engine_ready, remove_file) are declared here by its own names.
//
// Allocations: the wrapper's alone. thread: as connection.hpp states.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "qiven/memory/memory.hpp"
#include "qiven/memory/pool.hpp"
#include "qiven/recordlog/codes.hpp"
#include "qiven/storage_sqlite/connection.hpp"

namespace qiven::recordlog::sqlite {

using qiven::storage_sqlite::engine_ready;
using qiven::storage_sqlite::engine_version;
using qiven::storage_sqlite::remove_file;
using qiven::storage_sqlite::Setting;
using qiven::storage_sqlite::setting;
using qiven::storage_sqlite::When;

// The process's engine heap, as the wrapper's: its making refuses,
// StoreError internal naming open_name, while another is installed, when
// the pages cannot hold the slots or when the engine refuses its
// configuration.
class EngineHeap {
public:
    EngineHeap(std::span<std::byte> pages, std::size_t connections);
    EngineHeap(const EngineHeap&) = delete;
    EngineHeap& operator=(const EngineHeap&) = delete;
    EngineHeap(EngineHeap&&) = delete;
    EngineHeap& operator=(EngineHeap&&) = delete;
    ~EngineHeap() = default;

    [[nodiscard]] qiven::memory::Heap& heap() noexcept { return heap_.heap(); }
    [[nodiscard]] qiven::storage_sqlite::Slots slots() const noexcept { return heap_.slots(); }

private:
    qiven::storage_sqlite::EngineHeap heap_;
};

class Connection;

// A prepared statement, as the wrapper's.
class Statement {
public:
    Statement() noexcept = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) noexcept = default;
    Statement& operator=(Statement&&) noexcept = default;
    ~Statement() = default;

    void bind(int index, std::int64_t value, const WhereView& at);
    void bind_text(int index, std::string_view text, const WhereView& at);
    void bind_blob(int index, std::string_view bytes, const WhereView& at);
    void bind_null(int index, const WhereView& at);
    bool step(const WhereView& at);
    // A step's own extended code, unmapped: 100 a row, 101 done, else the
    // failure the store maps itself (an insert's HeadConflict, its actual
    // head read again first).
    [[nodiscard]] int step_code() noexcept { return statement_.step_code(); }
    void reset() noexcept { statement_.reset(); }
    [[nodiscard]] std::int64_t integer(int column) const noexcept { return statement_.integer(column); }
    [[nodiscard]] std::string_view text(int column) const noexcept { return statement_.text(column); }
    [[nodiscard]] std::string_view blob(int column) const noexcept { return statement_.blob(column); }
    [[nodiscard]] bool null(int column) const noexcept { return statement_.null(column); }

private:
    friend class Connection;
    explicit Statement(qiven::storage_sqlite::Statement statement) noexcept : statement_(std::move(statement)) {}

    qiven::storage_sqlite::Statement statement_;
};

// One connection, as the wrapper's, opened with the store's busy wait.
class Connection {
public:
    Connection() noexcept = default;
    static Connection open(const qiven::memory::String& path, bool read_only, const WhereView& at);
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;
    ~Connection() = default;

    [[nodiscard]] Statement prepare(std::string_view sql, const WhereView& at);
    void exec(std::string_view sql, const WhereView& at);
    [[nodiscard]] std::int64_t integer(std::string_view sql, const WhereView& at);
    [[noreturn]] void raise(int code, const WhereView& at) const;
    // A purge's statement, which never throws (the wrapper's run).
    [[nodiscard]] int run(std::string_view sql, std::span<std::int64_t> row = {}) noexcept {
        return connection_.run(sql, row);
    }
    void wait(int milliseconds) noexcept { connection_.wait(milliseconds); }
    [[nodiscard]] std::int64_t changes() const noexcept { return connection_.changes(); }
    [[nodiscard]] bool in_transaction() const noexcept { return connection_.in_transaction(); }
    void rollback() noexcept { connection_.rollback(); }
    [[nodiscard]] std::string_view filename() const noexcept { return connection_.filename(); }
    [[nodiscard]] bool is_open() const noexcept { return connection_.is_open(); }
    void close() noexcept { connection_.close(); }

private:
    explicit Connection(qiven::storage_sqlite::Connection connection) noexcept
        : connection_(std::move(connection)) {}

    qiven::storage_sqlite::Connection connection_;
};

}  // namespace qiven::recordlog::sqlite
