// sqlite_connection: the store's thin values over the wrapper's
// (sqlite_connection.hpp), and the one mapping of the wrapper's EngineError
// to the store's StoreError; and the store's countdowns (sqlite_steps.hpp),
// the engine calls' given on to the wrapper.

#include "qiven/recordlog/sqlite_connection.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "qiven/memory/memory.hpp"
#include "qiven/recordlog/codes.hpp"
#include "qiven/recordlog/errors.hpp"
#include "qiven/recordlog/sqlite_steps.hpp"
#include "qiven/storage_sqlite/connection.hpp"

namespace qiven::recordlog::sqlite {

namespace {

using qiven::storage_sqlite::EngineError;
using qiven::storage_sqlite::Refusal;
using qiven::storage_sqlite::Site;

Site site_of(const WhereView& at) noexcept { return Site{at.operation, at.path}; }

// The store's error for the wrapper's at the store's site.
StoreError mapped(const EngineError& e, const WhereView& at) noexcept {
    if (e.refusal() == Refusal::engine) {
        return to_error(e.code(), at);
    }
    if (e.refusal() == Refusal::configuration) {
        std::array<char, result_detail_bytes> buffer{};
        return StoreError(Code::internal, at.operation, at.path, result_detail(e.code(), buffer));
    }
    return StoreError(Code::internal, at.operation, at.path, e.detail());
}

// f called with the wrapper's site, an EngineError it throws mapped.
template <class F>
decltype(auto) mapping(const WhereView& at, F&& f) {
    try {
        return std::forward<F>(f)(site_of(at));
    } catch (const EngineError& e) {
        throw mapped(e, at);
    }
}

#if defined(QIVEN_FAULTS)
std::atomic<Countdown*>& arenas() noexcept {
    static std::atomic<Countdown*> countdown{nullptr};
    return countdown;
}
#endif

}  // namespace

#if defined(QIVEN_FAULTS)
void attach_calls(Countdown* calls, Countdown* arenas_too) noexcept {
    qiven::storage_sqlite::attach_calls(calls);
    arenas().store(arenas_too, std::memory_order_release);
}

Countdown* arenas_countdown() noexcept { return arenas().load(std::memory_order_acquire); }
#endif

// The engine heap's making, its refusals mapped at open_name.
EngineHeap::EngineHeap(std::span<std::byte> pages, std::size_t connections) try : heap_(pages, connections) {
} catch (const EngineError& e) {
    WhereView at;
    at.operation = open_name;
    throw mapped(e, at);
}

void Statement::bind(int index, std::int64_t value, const WhereView& at) {
    mapping(at, [&](const Site& s) { statement_.bind(index, value, s); });
}

void Statement::bind_text(int index, std::string_view text, const WhereView& at) {
    mapping(at, [&](const Site& s) { statement_.bind_text(index, text, s); });
}

void Statement::bind_blob(int index, std::string_view bytes, const WhereView& at) {
    mapping(at, [&](const Site& s) { statement_.bind_blob(index, bytes, s); });
}

void Statement::bind_null(int index, const WhereView& at) {
    mapping(at, [&](const Site& s) { statement_.bind_null(index, s); });
}

bool Statement::step(const WhereView& at) {
    return mapping(at, [&](const Site& s) { return statement_.step(s); });
}

Connection Connection::open(const qiven::memory::String& path, bool read_only, const WhereView& at) {
    return Connection(mapping(at, [&](const Site& s) {
        return qiven::storage_sqlite::Connection::open(path, read_only, busy_milliseconds, s);
    }));
}

Statement Connection::prepare(std::string_view sql, const WhereView& at) {
    return Statement(mapping(at, [&](const Site& s) { return connection_.prepare(sql, s); }));
}

void Connection::exec(std::string_view sql, const WhereView& at) {
    mapping(at, [&](const Site& s) { connection_.exec(sql, s); });
}

std::int64_t Connection::integer(std::string_view sql, const WhereView& at) {
    return mapping(at, [&](const Site& s) { return connection_.integer(sql, s); });
}

void Connection::raise(int code, const WhereView& at) const {
    try {
        connection_.raise(code, site_of(at));
    } catch (const EngineError& e) {
        throw mapped(e, at);
    }
}

}  // namespace qiven::recordlog::sqlite
