#pragma once

// sqlite_store: the SQLite store, SQLiteStore<P, H>, a template on the
// platform and on the step hooks, over the connection wrapper and the
// open steps of sqlite.hpp; and Engine<P>, the engine heap over pages the
// platform gives, which its maker keeps past every store it opens.
//
// open(path, create, read_only, platform, limits) runs the text's steps:
// the path resolved, the guard on it (a .git in it or above it, StoreError
// location checkout; a folder synced names, location synced), the engine's
// version; then the file's presence (open: missing, or corrupt at 0
// bytes; create: exists), create making the directory if absent and the
// file by the platform's create; then the connection, identify, the
// settings, create's schema and TRUNCATE checkpoint, the write probe on a
// read-write object, coherence. Errors name "open" and the resolved path.
// A pending purge's one attempt at open is the purge's (not built yet).
//
// Each operation runs Gate first. Built so far: open, guard, read_only,
// limits, close (the connection closed; its purge attempt and exports'
// close come with them). The others refuse after the gate, StoreError
// internal "not built", until their parts are built (sqlite.cpp's writes,
// exchange).
//
// Budgets: the object's own pages, 1 MiB, taken at open; open's resolved
// path and the synced client's name on an arena over them; the engine's
// heap apart (Engine). Allocations at open: the resolved path's and the
// client's strings where past their small size; the engine's on its heap;
// the file system reads' paths (sqlite.hpp). Platform calls at open:
// pages, resolve, synced, and at create one or two creates.
//
// thread: one, each object (Gate); Engine's heap shared.

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/sqlite.hpp"

namespace qiven3::store {

// The engine heap over the platform's pages: the engine's budget, a
// connection's, times connections. Made before the first open and kept
// until every store it served is closed; one at a time in a process.
template <platform::Platform P>
class Engine {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));

    explicit Engine(P& platform, std::size_t connections = sqlite::default_connections)
        : pages_(platform.pages(bytes(connections))), heap_(pages_.bytes()) {}
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    ~Engine() = default;

    [[nodiscard]] memory::Heap& heap() noexcept { return heap_.heap(); }

private:
    static std::size_t bytes(std::size_t connections) {
        const std::optional<std::size_t> n = memory::mul(engine_budget, connections);
        if (!n || connections == 0) {
            throw memory::MemoryError::budget("store/sqlite", engine_budget, connections, 0);
        }
        return *n;
    }

    Pages pages_;  // before the heap, which is made over them and ends first
    sqlite::EngineHeap heap_;
};

namespace sqlite {

// A scan or an export of a part not built yet: each call refuses.
class Unbuilt {
public:
    Unbuilt() noexcept = default;
    Unbuilt(const Unbuilt&) = delete;
    Unbuilt& operator=(const Unbuilt&) = delete;
    Unbuilt(Unbuilt&&) noexcept = default;
    Unbuilt& operator=(Unbuilt&&) noexcept = default;
    ~Unbuilt() = default;

    [[noreturn]] static bool next(Header& /*out*/) { refuse(); }
    [[noreturn]] static bool next(Line& /*line*/) { refuse(); }
    [[noreturn]] static Snapshot heads(Vector<LaneHead>& /*rows*/) { refuse(); }
    static void close() noexcept {}

private:
    [[noreturn]] static void refuse() { throw StoreError(Code::internal, "scan", {}, "not built"); }
};

}  // namespace sqlite

template <platform::Platform P, Hooks H = NoHooks>
class SQLiteStore {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));
    static constexpr Capabilities capabilities{true, true, true};
    // The settings each connection applies and reads back.
    static constexpr const auto& SETTINGS = sqlite::settings;

    // Opens, or creates, the store at path (sqlite_store.hpp's steps); an
    // Engine must be installed. Read-only, create is refused.
    static SQLiteStore open(std::string_view path, bool create, bool read_only, P& platform,
                            const Limits& limits = Limits{}, H hooks = H{}) {
        if (!valid(limits)) {
            throw StoreError(Code::settings, sqlite::open_name, path, "limits");
        }
        if (create && read_only) {
            throw StoreError(Code::read_only, sqlite::open_name, path, "create");
        }
        SQLiteStore store(platform, read_only, limits, std::move(hooks));
        {
            memory::Arena arena(store.pages_.bytes());
            String resolved{memory::Allocator<char>(arena)};
            platform.resolve(path, resolved);
            store.path_ = memory::Inline<path_bytes>(resolved);
            const WhereView at = sqlite::site(sqlite::open_name, resolved);
            check(resolved, platform, arena, sqlite::open_name);
            if (sqlite::engine_version() < sqlite::least_version) {
                throw StoreError(Code::engine_version, sqlite::open_name, resolved, "older than 3.37.0");
            }
            if (!sqlite::engine_ready()) {
                throw StoreError(Code::internal, sqlite::open_name, resolved, "no engine heap installed");
            }
            sqlite::presence(resolved, create, at);
            if (create) {
                make(resolved, platform, at);
            }
            store.connection_ = sqlite::connect(resolved, create, read_only, at, store.opened_);
        }
        if (!read_only && store.opened_.pending()) {
            store.purge_ = store.attempt();
        }
        return store;
    }

    // The guard alone, on path resolved: StoreError location, checkout or
    // synced, naming "guard".
    static void guard(std::string_view path, P& platform) {
        Pages pages = platform.pages(operation_base);
        memory::Arena arena(pages.bytes());
        String resolved{memory::Allocator<char>(arena)};
        platform.resolve(path, resolved);
        check(resolved, platform, arena, sqlite::guard_name);
    }

    [[nodiscard]] static int engine_version() noexcept { return sqlite::engine_version(); }

    SQLiteStore(const SQLiteStore&) = delete;
    SQLiteStore& operator=(const SQLiteStore&) = delete;
    // A moved-from object is closed.
    SQLiteStore(SQLiteStore&& other) noexcept
        : platform_(other.platform_), pages_(std::move(other.pages_)), gate_(other.gate_), limits_(other.limits_),
          path_(other.path_), connection_(std::move(other.connection_)), opened_(other.opened_),
          purge_(other.purge_), hooks_(std::move(other.hooks_)) {
        other.gate_.close();
    }
    SQLiteStore& operator=(SQLiteStore&& other) noexcept {
        if (this != &other) {
            platform_ = other.platform_;
            pages_ = std::move(other.pages_);
            gate_ = other.gate_;
            limits_ = other.limits_;
            path_ = other.path_;
            connection_ = std::move(other.connection_);
            opened_ = other.opened_;
            purge_ = other.purge_;
            hooks_ = std::move(other.hooks_);
            other.gate_.close();
        }
        return *this;
    }
    // The connection closed by its own end, without the gate.
    ~SQLiteStore() = default;

    Written append(std::string_view /*scope*/, std::string_view /*lane*/, Expected /*expected*/,
                   std::span<const EntryView> /*entries*/) {
        unbuilt(Operation::append);
    }
    std::optional<Head> head(std::string_view /*scope*/, std::string_view /*lane*/) { unbuilt(Operation::head); }
    Heads heads(Scopes /*scopes*/, Vector<LaneHead>& /*rows*/) { unbuilt(Operation::heads); }
    void header(std::string_view /*id*/, Header& /*out*/) { unbuilt(Operation::header); }
    bool body(std::string_view /*id*/, String& /*record*/) { unbuilt(Operation::body); }
    bool forgotten(std::string_view /*id*/) { unbuilt(Operation::forgotten); }
    sqlite::Unbuilt scan(const RangeView& /*range*/) { unbuilt(Operation::scan); }
    Written forget(std::string_view /*scope*/, std::string_view /*lane*/, Expected /*expected*/,
                   std::span<const EntryView> /*entries*/, std::span<const GuardView> /*guard*/,
                   Vector<Id>& /*erased*/) {
        unbuilt(Operation::forget);
    }
    Purge purge() { unbuilt(Operation::purge); }
    sqlite::Unbuilt export_(Scopes /*scopes*/, std::span<const AfterView> /*after*/) {
        unbuilt(Operation::export_);
    }
    template <Source R>
    Imported import_(R& /*source*/, Vector<LaneHead>& /*heads*/) {
        unbuilt(Operation::import_);
    }
    Sizes size() { unbuilt(Operation::size); }
    Counts verify() { unbuilt(Operation::verify); }

    // The gate, then the connection closed. Its purge attempt and its
    // exports' close come with them.
    Purge close() {
        gate_.enter(Operation::close, path_.view());
        gate_.close();
        connection_.close();
        return Purge{};
    }

    [[nodiscard]] bool read_only() const noexcept { return gate_.read_only(); }
    [[nodiscard]] Limits limits() const noexcept { return limits_; }
    // The store's resolved path, cut at path_bytes.
    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }
    // What open read, and its purge's result.
    [[nodiscard]] const sqlite::Opened& opened() const noexcept { return opened_; }
    [[nodiscard]] Purge opened_purge() const noexcept { return purge_; }
    // The connection, for the store's own modules (exchange) and cases.
    [[nodiscard]] sqlite::Connection& connection() noexcept { return connection_; }

private:
    SQLiteStore(P& platform, bool read_only, const Limits& limits, H hooks)
        : platform_(&platform), pages_(platform.pages(operation_base)), gate_(read_only), limits_(limits),
          hooks_(std::move(hooks)) {
        if (pages_.bytes().size() < operation_base) {
            memory::fail_fast("store/sqlite: pages short of their request");
        }
    }

    // The guard on a resolved path, its strings on arena.
    static void check(std::string_view resolved, P& platform, memory::Arena& arena, std::string_view op) {
        if (sqlite::in_checkout(resolved)) {
            throw StoreError(Code::location, op, resolved, "checkout");
        }
        String client{memory::Allocator<char>(arena)};
        if (platform.synced(resolved, client)) {
            throw StoreError(Code::location, op, resolved, "synced");
        }
    }

    // create's directory, made if absent, and its file, made only if
    // absent: one made meanwhile is StoreError exists.
    static void make(std::string_view resolved, P& platform, const WhereView& at) {
        const std::string_view directory = sqlite::directory_of(resolved);
        if (!directory.empty() && !sqlite::is_directory(directory)) {
            try {
                platform.create(directory, true);
            } catch (const platform::PlatformError& e) {
                if (e.code != platform::Code::exists) {
                    throw;
                }
            }
        }
        try {
            platform.create(resolved, false);
        } catch (const platform::PlatformError& e) {
            if (e.code == platform::Code::exists) {
                throw StoreError(Code::exists, at.operation, at.path, "a file was made meanwhile");
            }
            throw;
        }
    }

    // The pending purge's one attempt at open: the purge's part.
    Purge attempt() { unbuilt(Operation::purge); }

    [[noreturn]] void unbuilt(Operation op) const {
        gate_.enter(op, path_.view());
        throw StoreError(Code::internal, name(op), path_.view(), "not built");
    }

    P* platform_;
    Pages pages_;
    Gate gate_;
    Limits limits_;
    memory::Inline<path_bytes> path_;
    sqlite::Connection connection_;
    sqlite::Opened opened_;
    Purge purge_;
    QIVEN3_NO_UNIQUE_ADDRESS H hooks_;
};

}  // namespace qiven3::store
