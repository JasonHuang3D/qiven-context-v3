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
// A pending purge's one attempt at open, on a read-write object.
//
// Each operation runs Gate first. append and forget: stage's check outside
// the transaction; BEGIN IMMEDIATE (hook begun); the head; at forget the
// guard; part 2 against WriteView, F138's clearing one direction record at
// a time on check's scratch; at forget whole records and each target's
// body erased; the rows, the head, meta (hook written); COMMIT (hook
// committed); then a purge's one attempt. A failure in between rolls back.
// purge and close: the attempt (steps a to c), which raises no engine
// result; a read-only object attempts none. size: meta, then the figures.
// head, heads, header, body, forgotten: one query each through
// sqlite_read.cpp (heads in one read transaction), header and body
// re-checking what they return; scan a cursor (sqlite::ScanView). export,
// import and verify still refuse after the gate, StoreError internal "not
// built".
//
// Budgets: the object's own pages, 1 MiB, taken at open; open's resolved
// path and the synced client's name on an arena over them. append and
// forget: 1 MiB plus 4 times the lines given (the object's own where they
// hold it), halved: the kept arena (check's entries, the rows, the
// targets, part 2's), and check's scratch, afresh for each line, then each
// direction record's clearing. header, body and a scan's next: an arena
// over the object's own pages. The engine's heap apart (Engine). Platform
// calls: at open pages, resolve, synced, at create one or two creates; at
// append and forget pages past 1 MiB, and part 1's wipes.
//
// thread: one, each object (Gate); Engine's heap shared.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/sqlite.hpp"
#include "store/stage.hpp"

namespace qiven3::store {

// The engine heap over the platform's pages: the engine's budget, a
// connection's, times connections. Made before the first open and kept
// until every store it served is closed; one at a time in a process.
template <platform::Platform P>
class Engine {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));

    explicit Engine(P& platform, std::size_t connections = sqlite::default_connections)
        : pages_(platform.pages(bytes(connections))), heap_(pages_.bytes(), connections) {}
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;
    ~Engine() = default;

    [[nodiscard]] memory::Heap& heap() noexcept { return heap_.heap(); }
    [[nodiscard]] sqlite::Slots slots() const noexcept { return heap_.slots(); }

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

// An export, not built yet: each call refuses.
class Unbuilt {
public:
    Unbuilt() noexcept = default;
    Unbuilt(const Unbuilt&) = delete;
    Unbuilt& operator=(const Unbuilt&) = delete;
    Unbuilt(Unbuilt&&) noexcept = default;
    Unbuilt& operator=(Unbuilt&&) noexcept = default;
    ~Unbuilt() = default;

    [[noreturn]] static bool next(Line& /*line*/) { refuse(); }
    [[noreturn]] static Snapshot heads(Vector<LaneHead>& /*rows*/) { refuse(); }
    static void close() noexcept {}

private:
    [[noreturn]] static void refuse() { throw StoreError(Code::internal, "export", {}, "not built"); }
};

}  // namespace sqlite

template <platform::Platform P, Hooks H = NoHooks>
class SQLiteStore {
    using Slot = entry::header::Slot;

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
            store.hook(arena);
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
            store.peak_ = arena.peak();
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
#if defined(QIVEN3_FAULTS)
        if (sqlite::arenas_countdown() != nullptr) {
            arena.attach(sqlite::arenas_countdown());
        }
#endif
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
          purge_(other.purge_), last_(other.last_), countdown_(other.countdown_), peak_(other.peak_),
          hooks_(std::move(other.hooks_)) {
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
            last_ = other.last_;
            countdown_ = other.countdown_;
            peak_ = other.peak_;
            hooks_ = std::move(other.hooks_);
            other.gate_.close();
        }
        return *this;
    }
    // The connection closed by its own end, without the gate.
    ~SQLiteStore() = default;

    // The text's steps 1 to 7: check outside the transaction, then BEGIN
    // IMMEDIATE, the head, part 2, the rows, COMMIT, a purge's attempt.
    Written append(std::string_view scope, std::string_view lane, Expected expected,
                   std::span<const EntryView> entries) {
        return write(Operation::append, scope, lane, expected, entries, {}, nullptr);
    }
    std::optional<Head> head(std::string_view scope, std::string_view lane) {
        gate_.enter(Operation::head, path_.view());
        return sqlite::head_of(connection_, scope, lane, site_of(Operation::head));
    }
    // A purge pending: this object's last attempt's reason, else writer's.
    Heads heads(Scopes scopes, Vector<LaneHead>& rows) {
        gate_.enter(Operation::heads, path_.view());
        const sqlite::HeadsRead r = sqlite::heads(connection_, scopes, rows, site_of(Operation::heads));
        return Heads{r.forgets, !r.pending ? Purge{} : (last_.done ? Purge::pending("writer") : last_)};
    }
    void header(std::string_view id, Header& out) {
        memory::Arena arena(own(Operation::header));
        hook(arena);
        Header h{sqlite::Chars(arena)};
        found(sqlite::find(connection_, id, h, nullptr, site_of(Operation::header)), Operation::header, id);
        sqlite::copy_header(h, out);
        peak_ = arena.peak();
    }
    // false: forgotten, a forget, or a header with no body; else the record
    // checked against its header.
    bool body(std::string_view id, String& record) {
        constexpr Operation op = Operation::body;
        memory::Arena arena(own(op));
        hook(arena);
        const sqlite::Chars chars(arena);
        Header h(chars);
        String held(chars);
        const bool has = found(sqlite::find(connection_, id, h, &held, site_of(op)), op, id) == sqlite::Found::body;
        if (has) {
            checked(held, h, id, chars);
            record.assign(std::string_view(held));  // on record's allocator
        }
        peak_ = arena.peak();
        return has;
    }
    bool forgotten(std::string_view id) {
        gate_.enter(Operation::forgotten, path_.view());
        return sqlite::forgotten(connection_, id, site_of(Operation::forgotten));
    }
    // A cursor over this object's connection and pages (sqlite.hpp).
    sqlite::ScanView scan(const RangeView& range) {
        gate_.enter(Operation::scan, path_.view());
        return {gate_, connection_, pages_.bytes().first(operation_base), countdown_, path_.view(), range};
    }
    // As append, with the guard, whole records and each target's body
    // erased, its id into erased; never refused for size or count.
    Written forget(std::string_view scope, std::string_view lane, Expected expected,
                   std::span<const EntryView> entries, std::span<const GuardView> guard, Vector<Id>& erased) {
        return write(Operation::forget, scope, lane, expected, entries, guard, &erased);
    }
    Purge purge() {
        gate_.enter(Operation::purge, path_.view());
        return attempt();
    }
    sqlite::Unbuilt export_(Scopes /*scopes*/, std::span<const AfterView> /*after*/) {
        unbuilt(Operation::export_);
    }
    template <Source R>
    Imported import_(R& /*source*/, Vector<LaneHead>& /*heads*/) {
        unbuilt(Operation::import_);
    }
    // meta's size and count against the limits; the engine's figures and
    // the files' sizes.
    Sizes size() {
        gate_.enter(Operation::size, path_.view());
        const WhereView where = sqlite::site(name(Operation::size), path_.view());
        const sqlite::Totals t = sqlite::totals(connection_, where);
        Sizes s;
        s.volume = measure(static_cast<std::uint64_t>(t.size), static_cast<std::uint64_t>(t.count), limits_);
        sqlite::figures(connection_, s, where);
        return s;
    }
    Counts verify() { unbuilt(Operation::verify); }

    // The gate, a purge's attempt on a read-write object, the connection
    // closed, whatever the attempt raised. Its exports' close is exchange's.
    Purge close() {
        gate_.enter(Operation::close, path_.view());
        Purge p;
        try {
            if (!gate_.read_only()) {
                p = attempt();
            }
        } catch (...) {
            gate_.close();
            connection_.close();
            throw;
        }
        gate_.close();
        connection_.close();
        return p;
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
    // The counted peak of the last open, append, forget, header or body
    // that returned: its arenas' high-water marks summed.
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }
#if defined(QIVEN3_FAULTS)
    // Each arena of an operation asks countdown first.
    void attach(sqlite::Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    SQLiteStore(P& platform, bool read_only, const Limits& limits, H hooks)
        : platform_(&platform), pages_(platform.pages(operation_base)), gate_(read_only), limits_(limits),
          hooks_(std::move(hooks)) {
        if (pages_.bytes().size() < operation_base) {
            memory::fail_fast("store/sqlite: pages short of their request");
        }
#if defined(QIVEN3_FAULTS)
        countdown_ = sqlite::arenas_countdown();
#endif
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

    // append's and forget's steps; erased given at forget alone.
    Written write(Operation op, std::string_view scope, std::string_view lane, Expected expected,
                  std::span<const EntryView> entries, std::span<const GuardView> guard, Vector<Id>* erased) {
        gate_.enter(op, path_.view());
        std::optional<Pages> extra;
        const std::span<std::byte> span = room(entries, extra);
        const std::size_t half = span.size() / 2;
        memory::Arena kept(span.first(span.size() - half));
        hook(kept);
        sqlite::ScratchView scratch(span.subspan(span.size() - half), countdown_);
        const sqlite::Chars chars(kept);
        const stage::AtView at{op, path_.view()};
        const stage::Checked<P> checked = stage::check(*platform_, entries, at, chars, scratch);
        Vector<sqlite::RowView> rows(chars);
        Vector<Id> targets(chars);
        rows.reserve(entries.size());
        targets.reserve(erased != nullptr ? entries.size() : 0);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const Header& h = checked.entries[i].header();
            rows.push_back(sqlite::RowView{h.value(Slot::id), h.value(Slot::scope), h.value(Slot::lane),
                                           h.value(Slot::kind), h.text(Slot::parent), h.text(Slot::target), h.seq(),
                                           entries[i].line, entries[i].body});
            if (erased != nullptr) {
                targets.push_back(Id::of(h.value(Slot::target)));
            }
        }
        WhereView where = sqlite::site(name(op), path_.view());
        connection_.exec("BEGIN IMMEDIATE", where);
        Written w;
        try {
            hooks_.begun();
            const std::optional<Head> now = sqlite::expect(connection_, scope, lane, expected, where);
            if (erased != nullptr) {
                sqlite::guard(connection_, guard, where);
            }
            const sqlite::Totals held = sqlite::totals(connection_, where);
            sqlite::WriteView view(connection_, chars, &scratch, where, held);
            const stage::Staged staged = stage::admit(view, scope, lane, expected, checked, limits_, at, chars);
            std::uint64_t dropped = 0;
            if (erased != nullptr) {
                sqlite::whole_record(connection_, targets, where);
                dropped = sqlite::erase(connection_, targets, *erased, where);
            }
            const std::uint64_t size = stage::detail::sum(static_cast<std::uint64_t>(held.size), staged.size);
            const std::uint64_t count = stage::detail::sum(static_cast<std::uint64_t>(held.count), staged.count);
            where.size = size;
            where.count = count;
            where.max_size = limits_.max_size;
            where.max_count = limits_.max_count;
            sqlite::insert(connection_, rows, expected, where);
            if (!entries.empty()) {
                sqlite::advance(connection_, scope, lane, expected, staged.head, where);
            }
            const std::uint64_t forgets = erased != nullptr ? entries.size() : 0;
            sqlite::add_meta(connection_, delta(staged.size, dropped), delta(staged.count, 0), delta(forgets, 0),
                             where);
            hooks_.written();
            connection_.exec("COMMIT", where);
            w.head = entries.empty() ? now.value_or(Head{}) : staged.head;
            w.volume = measure(size - std::min(size, dropped), count, limits_);
        } catch (...) {
            connection_.rollback();
            throw;
        }
        hooks_.committed();
        peak_ = kept.peak() + scratch.peak();
        w.purge = attempt();
        return w;
    }

    // added less dropped, as meta's signed figure.
    static std::int64_t delta(std::uint64_t added, std::uint64_t dropped) {
        constexpr auto most = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (added > most || dropped > most) {
            throw memory::MemoryError::budget("store/sqlite", most, std::max(added, dropped), 0);
        }
        return static_cast<std::int64_t>(added) - static_cast<std::int64_t>(dropped);
    }

    // An operation's pages, 1 MiB plus 4 times the lines given: the
    // object's own where they hold it, else pages the call takes.
    std::span<std::byte> room(std::span<const EntryView> entries, std::optional<Pages>& extra) {
        const std::optional<std::size_t> lines = given(entries);
        const std::optional<std::size_t> budget = lines ? operation_budget(*lines) : std::nullopt;
        if (!budget) {
            throw memory::MemoryError::budget("store/sqlite", std::numeric_limits<std::size_t>::max(),
                                              entries.size(), 0);
        }
        if (*budget <= operation_base) {
            return pages_.bytes().first(*budget);
        }
        extra.emplace(platform_->pages(*budget));
        if (extra->bytes().size() < *budget) {
            memory::fail_fast("store/sqlite: pages short of their request");
        }
        return extra->bytes().first(*budget);
    }

    void hook([[maybe_unused]] memory::Arena& arena) const noexcept {
#if defined(QIVEN3_FAULTS)
        if (countdown_ != nullptr) {
            arena.attach(countdown_);
        }
#endif
    }

    // A purge's one attempt, on the store's connection with no statement
    // or transaction open, its busy wait 0 and restored after: never an
    // engine result raised, never waiting.
    Purge attempt() {
        connection_.wait(0);
        Purge p;
        try {
            p = attempt_steps();
        } catch (...) {
            connection_.wait(sqlite::busy_milliseconds);
            throw;
        }
        connection_.wait(sqlite::busy_milliseconds);
        last_ = p;
        return p;
    }

    // A read's gate, then its arena's span: the object's own pages.
    std::span<std::byte> own(Operation op) {
        gate_.enter(op, path_.view());
        return pages_.bytes().first(operation_base);
    }
    [[nodiscard]] WhereView site_of(Operation op) const noexcept { return sqlite::site(name(op), path_.view()); }
    sqlite::Found found(sqlite::Found f, Operation op, std::string_view id) const {
        if (f == sqlite::Found::absent) {
            throw StoreError::not_found(name(op), path_.view(), id);
        }
        return f;
    }
    // A held body record against its header: one the parser refuses
    // StoreCorrupt incoherent, one not its header's body_mismatch.
    void checked(std::string_view record, const Header& h, std::string_view id, const sqlite::Chars& chars) {
        namespace body = entry::body;
        try {
            body::check(body::parse(*platform_, record, chars), h);
        } catch (const body::BodyError& f) {
            const bool mismatch = f.code == body::Code::body_mismatch;
            throw StoreError::corrupt(name(Operation::body), path_.view(),
                                      mismatch ? Corrupt::body_mismatch : Corrupt::incoherent, id);
        } catch (const ParseError&) {
            throw StoreError::corrupt(name(Operation::body), path_.view(), Corrupt::incoherent, id);
        }
    }

    // Steps a to c; a step a result stopped, pending with its reason.
    Purge attempt_steps() {
        sqlite::Connection& c = connection_;
        const auto pending = [](int code, Step step) { return Purge::pending(purge_reason(code, step)); };
        std::array<std::int64_t, 3> m{};
        int code = c.run("SELECT (SELECT value FROM meta WHERE name = 'forgets'), (SELECT value FROM meta WHERE "
                         "name = 'vacuumed'), (SELECT value FROM meta WHERE name = 'purged')",
                         m);
        if (code != 0) {
            return pending(code, Step::read);
        }
        const std::int64_t forgets = m[0];
        std::int64_t vacuumed = m[1];
        if (m[2] >= forgets) {
            return Purge{};
        }
        std::array<char, 96> sql{};
        if (vacuumed < forgets) {
            code = c.run("VACUUM");
            if (code != 0) {
                return pending(code, Step::vacuum);
            }
            hooks_.rebuilt();
            code = c.run(sqlite::raise_meta("vacuumed", forgets, sql));
            if (code != 0) {
                return pending(code, Step::vacuumed);
            }
            vacuumed = forgets;
        }
        std::array<std::int64_t, 1> busy{};
        code = c.run("PRAGMA wal_checkpoint(TRUNCATE)", busy);
        if (code != 0) {
            return pending(code, Step::checkpoint);
        }
        if (busy[0] != 0) {
            code = c.run("BEGIN IMMEDIATE");
            c.rollback();
            return pending(code, Step::probe);
        }
        if (sqlite::file_bytes(c.filename(), "-wal").value_or(0) != 0) {
            return Purge::pending("writer");
        }
        code = c.run(sqlite::raise_meta("purged", vacuumed, sql));
        if (code != 0) {
            return pending(code, Step::purged);
        }
        static_cast<void>(c.run("PRAGMA wal_checkpoint(TRUNCATE)"));
        return Purge{};
    }

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
    Purge last_;  // the last attempt's
    [[maybe_unused]] sqlite::Countdown* countdown_ = nullptr;
    std::size_t peak_ = 0;
    QIVEN3_NO_UNIQUE_ADDRESS H hooks_;
};

}  // namespace qiven3::store
