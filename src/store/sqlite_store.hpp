#pragma once

// sqlite_store: the SQLite store, SQLiteStore<P, H, Rule>, on the platform,
// the step hooks and its writes' default admit hook, over sqlite.hpp's
// connection wrapper and open steps; and Engine<P>, the engine heap over
// pages the platform gives, which its maker keeps past every store it opens.
//
// open(path, create, read_only, platform, limits) runs the text's steps:
// the path resolved, the guard on it (a .git in it or above it, StoreError
// location checkout; a folder synced names, location synced), the engine's
// version; then the file's presence (open: missing, or corrupt at 0
// bytes; create: exists), create making the directory if absent and the
// file by the platform's create; then the connection, identify, the
// settings, create's schema and TRUNCATE checkpoint, the write probe on a
// read-write object, coherence; a create refused removes the files and
// the empty directory it made, naming each removal refused in its error
// (sqlite_create.hpp). Errors name "open" and the resolved path.
// A pending purge's one attempt at open, on a read-write object.
//
// Each operation runs Gate first. append and forget: stage's read outside
// the transaction; BEGIN IMMEDIATE (hook begun); the head; at forget the
// guard; the admit hook's admit on WriteView (Rule's, the one a ruled
// store takes), the store's own order; at forget whole records and each
// target's body erased, each held in the call's scope with its body, once
// (stage's erasable); the rows, the head, meta (hook written); COMMIT (hook committed);
// then a purge's one attempt. A failure in between rolls back.
// purge and close: the attempt (steps a to c), which raises no engine
// result; a read-only object attempts none. size: meta, then the figures.
// head, heads, header, body, forgotten: one query each through
// sqlite_read.cpp (heads in one read transaction), header and body giving
// the bytes held, parsing neither; scan a cursor (sqlite::ScanView). export,
// import and verify: exchange.hpp's, an export closed by this object's
// close, move or end.
//
// Budgets: the object's own pages, 1 MiB, taken at open; open's resolved
// path and the synced client's name on an arena over them. append and
// forget: 1 MiB plus 4 times the lines given (the object's own where they
// hold it), halved: the kept arena (read's entries, the rows, the targets,
// the hook's), and read's scratch, afresh for each line, and the hook's.
// header, body and a scan's next: none, the bytes on the caller's; lent
// to a step, the bytes and the step's on an arena over the object's own
// pages.
// The engine's heap apart (Engine). Platform calls: at open pages,
// resolve, synced, at create one or two creates; at append and forget
// pages past 1 MiB, and the hook's (each hook says).
//
// thread: one, each object (Gate); Engine's heap shared.

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "qiven3/canon.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/exchange.hpp"
#include "store/sqlite_create.hpp"
#include "store/stage.hpp"

namespace qiven3::store {

// The engine heap over the platform's pages: the engine's budget, a
// connection's, times connections. Made before the first open and kept
// until every store it served is closed; one at a time in a process: one
// made while another is installed is refused, StoreError internal, before
// it takes its pages, with no platform call and no allocation, the
// installed one unchanged.
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
        // Before the pages; between two made at once, the engine heap's
        // own check, atomic, decides.
        if (sqlite::engine_ready()) {
            throw StoreError(Code::internal, sqlite::open_name, {}, "an engine heap is installed already");
        }
        const std::optional<std::size_t> n = memory::mul(engine_budget, connections);
        if (!n || connections == 0) {
            throw memory::MemoryError::budget("store/sqlite", engine_budget, connections, 0);
        }
        return *n;
    }

    Pages pages_;  // before the heap, which is made over them and ends first
    sqlite::EngineHeap heap_;
};

template <platform::Platform P, Hooks H = NoHooks, class Rule = NoAdmit>
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
            store.connection_ = create ? sqlite::made(resolved, platform, at, store.opened_)
                                       : sqlite::connect(resolved, false, read_only, at, store.opened_);
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
          exports_(std::move(other.exports_)), hooks_(std::move(other.hooks_)) {
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
            exports_ = std::move(other.exports_);
            hooks_ = std::move(other.hooks_);
            other.gate_.close();
        }
        return *this;
    }
    // Its exports and the connection closed by their own ends, without the
    // gate.
    ~SQLiteStore() = default;

    // The text's steps 1 to 7: check outside the transaction, then BEGIN
    // IMMEDIATE, the head, part 2, the rows, COMMIT, a purge's attempt.
    // The admit hook's given, read and kept before BEGIN IMMEDIATE, its
    // admit after the head and the guard, with the transaction's view.
    template <class A = Rule>
        requires HookOf<A, Rule> &&
                 Admit<std::remove_cvref_t<A>, P, sqlite::WriteView, stage::KeptView, sqlite::ScratchView>
    Written append(std::string_view scope, std::string_view lane, Expected expected,
                   std::span<const EntryView> entries, A&& admit = A{}) {
        static_assert(Admit<NoAdmit, P, sqlite::WriteView, stage::KeptView, sqlite::ScratchView>);
        return write(Operation::append, scope, lane, expected, entries, {}, nullptr, admit);
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
    // The header line's bytes into line, and the body record's into record
    // (false: forgotten, a forget, or no body), each on its allocator.
    void header(std::string_view id, String& line) {
        peak_ = 0;
        (void)bytes(Operation::header, id, &line, nullptr);
    }
    bool body(std::string_view id, String& record) {
        peak_ = 0;
        return bytes(Operation::body, id, nullptr, &record);
    }
    // The reads lent to a caller's step, as the memory store's: the bytes
    // and the step's on an arena over the own pages, reset after the step.
    template <class Step>
        requires std::invocable<Step&, std::string_view, const sqlite::Chars&>
    void header(std::string_view id, Step&& step) {
        lend(Operation::header, id, [&step](std::string_view line, const std::string_view*, const sqlite::Chars& c) {
            step(line, c);
        });
    }
    template <class Step>
        requires std::invocable<Step&, std::string_view, const std::string_view*, const sqlite::Chars&>
    bool body(std::string_view id, Step&& step) {
        return lend(Operation::body, id, step);
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
    template <class A = Rule>
        requires HookOf<A, Rule> &&
                 Admit<std::remove_cvref_t<A>, P, sqlite::WriteView, stage::KeptView, sqlite::ScratchView>
    Written forget(std::string_view scope, std::string_view lane, Expected expected,
                   std::span<const EntryView> entries, std::span<const GuardView> guard, Vector<Id>& erased,
                   A&& admit = A{}) {
        return write(Operation::forget, scope, lane, expected, entries, guard, &erased, admit);
    }
    Purge purge() {
        gate_.enter(Operation::purge, path_.view());
        return attempt();
    }
    // Its own read-only connection on this object's file (exchange.hpp).
    sqlite::SQLiteExport<P> export_(Scopes scopes, std::span<const AfterView> after) {
        gate_.enter(Operation::export_, path_.view());
        return {*platform_, exports_, connection_.filename(), path_.view(), limits_, scopes, after, countdown_};
    }
    // Into an empty store, all or nothing, on the object's own pages.
    // The admit hook's read, arrived, settled and heads inside the transaction.
    template <Source R, class A = Rule>
        requires HookOf<A, Rule> && AdmitImport<std::remove_cvref_t<A>, P, sqlite::ImportView>
    Imported import_(R& source, Vector<LaneHead>& heads, A&& admit = A{}) {
        constexpr Operation op = Operation::import_;
        static_assert(AdmitImport<NoAdmit, P, sqlite::ImportView>);
        const std::span<std::byte> pages = own(op);
        WhereView where = site_of(op);
        where.place = Place::import_;
        return sqlite::import_source(*platform_, connection_, source, heads, pages, countdown_, where, limits_,
                                     hooks_, peak_, admit);
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
    Counts verify() {
        gate_.enter(Operation::verify, path_.view());
        return sqlite::verify_store(connection_, site_of(Operation::verify), peak_);
    }

    // The gate, its exports closed, a purge's attempt on a read-write
    // object, the connection closed, whatever the attempt raised.
    Purge close() {
        gate_.enter(Operation::close, path_.view());
        exports_.close();
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
    // The platform it was opened on.
    [[nodiscard]] P& platform() const noexcept { return *platform_; }
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

    // append's and forget's steps; erased given at forget alone.
    template <class A>
    Written write(Operation op, std::string_view scope, std::string_view lane, Expected expected,
                  std::span<const EntryView> entries, std::span<const GuardView> guard, Vector<Id>* erased,
                  A& admit) {
        gate_.enter(op, path_.view());
        std::optional<Pages> extra;
        const std::span<std::byte> span = room(entries, extra);
        const std::size_t half = span.size() / 2;
        memory::Arena kept(span.first(span.size() - half));
        hook(kept);
        sqlite::ScratchView scratch(span.subspan(span.size() - half), countdown_);
        const Released<A> released(admit);
        const sqlite::Chars chars(kept);
        const stage::AtView at{op, path_.view()};
        const stage::Checked checked = stage::read(*platform_, entries, at, chars, scratch, admit);
        Vector<sqlite::RowView> rows(chars);
        Vector<Id> targets(chars);
        rows.reserve(entries.size());
        targets.reserve(erased != nullptr ? entries.size() : 0);
        for (const stage::KeptView& k : checked.kept) {
            const KeysView keys = k.keys();
            rows.push_back(sqlite::RowView{keys.id, keys.scope, keys.lane, keys.kind, keys.parent, keys.target,
                                           keys.seq, k.line, k.record, keys.body});
            if (erased != nullptr) {
                targets.push_back(k.target.value_or(Id{}));
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
            admit.admit(view, scope, lane, expected, std::span<const stage::KeptView>(checked.kept), at, chars);
            const stage::Staged staged = stage::guarded(view, checked, limits_, at, scope, lane, now);
            std::uint64_t dropped = 0;
            if (erased != nullptr) {
                dropped = sqlite::erase(connection_, targets, scope, *erased, where);
                stage::erasable(std::span<const stage::KeptView>(checked.kept), std::span<const Id>(*erased), at);
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
    // A read's bytes where given; body where it has a body row.
    bool bytes(Operation op, std::string_view id, String* line, String* record) {
        gate_.enter(op, path_.view());
        return found(sqlite::find(connection_, id, line, record, site_of(op)), op, id) == sqlite::Found::body;
    }
    // A lent read's bytes on an arena to give; the peak once give returns.
    template <class Give>
    bool lend(Operation op, std::string_view id, Give&& give) {
        memory::Arena arena(pages_.bytes().first(operation_base));
        hook(arena);
        const sqlite::Chars chars(arena);
        String line(chars);
        String held(chars);
        const bool has = bytes(op, id, &line, op == Operation::body ? &held : nullptr);
        const std::string_view record(held);
        give(std::string_view(line), has ? &record : nullptr, chars);
        peak_ = arena.peak();
        return has;
    }
    [[nodiscard]] WhereView site_of(Operation op) const noexcept { return sqlite::site(name(op), path_.view()); }
    sqlite::Found found(sqlite::Found f, Operation op, std::string_view id) const {
        if (f == sqlite::Found::absent) {
            throw StoreError::not_found(name(op), path_.view(), id);
        }
        return f;
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
    sqlite::Exports exports_;  // after the connection: ends first
    QIVEN3_NO_UNIQUE_ADDRESS H hooks_;
};

}  // namespace qiven3::store
