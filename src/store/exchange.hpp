#pragma once

// exchange: the SQLite store's export, import and verify, run through the
// connection wrapper (engine/sqlite.hpp), so that it neither includes the
// engine's header nor names the engine.
//
// Export: a connection of its own on the store's file, read-only (connect,
// identify, the settings), in one read transaction whose first read, at
// its making, fixes the snapshot: meta's size and count and the set's
// forget count. heads() reads the set's head rows in it. next() walks the
// head rows in (scope, lane) order, a lane of the set at a time, each a
// cursor over its entry rows after its seq to start after, left-joined to
// body, in seq order; each row checked as a scan's and copied into the
// caller's Line, forgotten a lookup on entry_target in the snapshot; a
// header with content, no body and no forget StoreCorrupt incoherent. Its
// scope set and seqs, views of the call, are packed into its own pages. It
// ends at its close, its store object's close, move or end (Exports), or
// its own end; then StoreError closed. Its close, as its reads, from its
// maker's thread alone: else StoreError thread.
//
// Import: the source's heads first; BEGIN IMMEDIATE, foreign keys
// deferred, StoreNotEmpty unless meta counts no entry; each line through
// part 1, the store's chain (stage's arrives) and the admit hook's arrived
// as it arrives, then its entry, body
// and head rows; once every line is in, each entry in the order inserted
// through the final view (the hook's settled), the heads given to the
// hook's heads with the source's, meta written from the rows,
// vacuumed and purged equal to forgets; COMMIT; a TRUNCATE checkpoint. A
// refusal rolls back, then the checkpoint; the caller's heads cleared.
//
// Verify: in one read transaction, the engine's integrity_check; each head
// row's lane in (scope, lane) order, its entries in seq order, each checked
// as a scan's, its body record against its header, part 1 again (a fault
// StoreCorrupt incoherent naming its code), its chain, its target held, no
// body for a forgotten id or a forget (forgotten_body), a body for every
// other entry with content, the head row its last entry; then the entry
// rows, the body rows and meta against what the walk counted.
//
// Budgets: export, pages of its own, its packed records then 1 MiB, a fresh
// arena over it for each next's checks; import, the object's own 1 MiB, a
// quarter for the source's heads, a quarter for the line in hand, the half
// for each line's or entry's work, afresh; verify, the object's own, a
// fresh arena for each entry. The engine's heap apart. Allocations: export,
// the path's copy at its making, each next the header's parse and the
// caller's Line, heads the caller's rows; import, the source's own into its
// Line, part 1's and the view's on the work arena, the caller's heads';
// verify, each entry's header, body and part 1. Platform calls: an
// export's pages; part 1's and a body's keys and wipes.
//
// thread: one, each object and each export (its own gate).

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/stage.hpp"

namespace qiven3::store::sqlite {

// A fresh arena asks the countdown first, where one is given.
inline void attach_to([[maybe_unused]] memory::Arena& arena, [[maybe_unused]] Countdown* countdown) noexcept {
#if defined(QIVEN3_FAULTS)
    if (countdown != nullptr) {
        arena.attach(countdown);
    }
#endif
}

// Whether an import defers foreign keys: yes, but where the hooks type
// says no (S24's seam, in the test tree).
template <class H>
constexpr bool defers() noexcept {
    if constexpr (requires { H::defer_foreign_keys; }) {
        return H::defer_foreign_keys;
    } else {
        return true;
    }
}

// The export's statements beside meta's (totals), each planned with no
// structure built in memory as large as its rows (S12's plan check): the
// forget entries by scope, by entry_forget; the head rows; a lane's entry
// rows after a seq, in seq order, row_header's columns, then the body
// record, whether a forget targets the entry (entry_target) and whether
// its target is held. verify walks by the last two too.
inline constexpr std::string_view forget_counts =
    "SELECT scope, count(*) FROM entry WHERE kind = 'forget' GROUP BY scope";
inline constexpr std::string_view head_rows = "SELECT scope, lane, id, seq FROM head ORDER BY scope, lane";
inline constexpr std::string_view lane_rows =
    "SELECT e.id, e.scope, e.lane, e.seq, e.parent, e.kind, e.target, e.line, b.record, "
    "EXISTS (SELECT 1 FROM entry AS f WHERE f.target = e.id AND f.kind = 'forget'), "
    "e.target IS NULL OR EXISTS (SELECT 1 FROM entry AS t WHERE t.id = e.target) "
    "FROM entry AS e LEFT JOIN body AS b ON b.id = e.id WHERE e.scope = ?1 AND e.lane = ?2 AND e.seq > ?3 "
    "ORDER BY e.seq";

class Exports;

// An export's engine half: its own connection, the cursor over the head
// rows and the one over its lane's entries, bound to the head row in hand;
// its gate; its link in its store object's list. Moved, it takes the
// other's place there, the other closed; closed, it leaves it.
class Reader {
public:
    Reader() noexcept = default;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Reader(Reader&& other) noexcept;
    Reader& operator=(Reader&& other) noexcept;
    ~Reader() { close(); }

    // The scope set and the seqs packed at pages' front, the connection
    // on file, the snapshot read; linked into owner's list first.
    void open(Exports& owner, std::span<std::byte> pages, std::string_view file, std::string_view path,
              const Limits& limits, const Scopes& scopes, std::span<const AfterView> after, Countdown* countdown);
    Snapshot heads(std::span<const std::byte> pages, Vector<LaneHead>& rows);
    bool next(std::span<std::byte> pages, Line& line);
    void close() noexcept;
    // The thread check alone (Gate::own), before the handle's close.
    void own(Operation op) const { gate_.own(op, path_.view()); }
    // The counted peak of its making's or its greatest next's arena.
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }

    // The pages for a scope set and seqs: their packed records, then 1 MiB.
    [[nodiscard]] static std::size_t pages_for(const Scopes& scopes, std::span<const AfterView> after);

private:
    friend class Exports;
    void unlink() noexcept;
    void take(Reader& other) noexcept;
    [[nodiscard]] bool in_set(std::span<const std::byte> pages, std::string_view scope) const;
    [[nodiscard]] std::int64_t after_of(std::span<const std::byte> pages, std::string_view scope,
                                        std::string_view lane) const;
    // The next lane of the set, its cursor bound; false past the last.
    bool lane(std::span<const std::byte> pages, const WhereView& at);
    [[nodiscard]] WhereView site() const noexcept;

    Exports* owner_ = nullptr;
    Reader* prev_ = nullptr;
    Reader* next_ = nullptr;
    Connection c_;
    Statement lanes_;
    Statement rows_;
    Snapshot snapshot_;
    Countdown* countdown_ = nullptr;
    std::size_t scopes_ = 0;  // packed scope records
    std::size_t afters_ = 0;  // packed seq records, after them
    std::size_t peak_ = 0;
    Gate gate_{false};
    bool every_ = true;  // no scope set: every lane
    bool in_lane_ = false;
    bool done_ = false;
    memory::Inline<path_bytes> path_;
};

// The exports a store object made and that are open: closed by its close,
// its move or its end, so that none outlives it open.
class Exports {
public:
    Exports() noexcept = default;
    Exports(const Exports&) = delete;
    Exports& operator=(const Exports&) = delete;
    // A moved-from store object is closed: its exports with it.
    Exports(Exports&& other) noexcept { other.close(); }
    Exports& operator=(Exports&& other) noexcept {
        if (this != &other) {
            close();
            other.close();
        }
        return *this;
    }
    ~Exports() { close(); }
    void close() noexcept;

private:
    friend class Reader;
    Reader* first_ = nullptr;
};

// The export's handle: its pages and its engine half. A move-only value.
template <platform::Platform P>
class SQLiteExport {
public:
    using Pages = decltype(std::declval<P&>().pages(std::size_t{}));

    SQLiteExport(P& platform, Exports& owner, std::string_view file, std::string_view path, const Limits& limits,
                 const Scopes& scopes, std::span<const AfterView> after, Countdown* countdown)
        : pages_(platform.pages(Reader::pages_for(scopes, after))) {
        if (pages_.bytes().size() < Reader::pages_for(scopes, after)) {
            memory::fail_fast("store/exchange: pages short of their request");
        }
        reader_.open(owner, pages_.bytes(), file, path, limits, scopes, after, countdown);
    }
    SQLiteExport(const SQLiteExport&) = delete;
    SQLiteExport& operator=(const SQLiteExport&) = delete;
    SQLiteExport(SQLiteExport&&) noexcept = default;
    SQLiteExport& operator=(SQLiteExport&&) noexcept = default;
    ~SQLiteExport() = default;

    Snapshot heads(Vector<LaneHead>& rows) { return reader_.heads(pages_.bytes(), rows); }
    bool next(Line& line) { return reader_.next(pages_.bytes(), line); }
    // From another thread than its maker's, StoreError thread, the export
    // left open; again after close, nothing.
    void close() {
        reader_.own(Operation::close);
        reader_.close();
    }
    [[nodiscard]] std::size_t peak() const noexcept { return reader_.peak(); }

private:
    Pages pages_;    // before the reader, which reads them
    Reader reader_;  // ends first: its connection closed
};

// The final view of an import: part 2's view of the transaction, and the
// first forget held of an id, in the order inserted.
class ImportView {
public:
    ImportView(Connection& c, const Chars& allocator, const WhereView& at) noexcept
        : c_(&c), view_(c, allocator, nullptr, at, Totals{}), at_(at) {}
    [[nodiscard]] std::optional<Head> head(std::string_view scope, std::string_view lane) {
        return view_.head(scope, lane);
    }
    bool header(std::string_view id, Header& out) { return view_.header(id, out); }
    bool body(std::string_view id, entry::validator::Text<Chars>& out) { return view_.body(id, out); }
    [[nodiscard]] bool forgotten(std::string_view id) { return view_.forgotten(id); }
    [[nodiscard]] std::optional<Id> forget_of(std::string_view id);

private:
    Connection* c_;
    WriteView view_;
    WhereView at_;
};

// import's statements: an entry's row, its body's, its lane's head row.
class ImportRows {
public:
    ImportRows(Connection& c, const WhereView& at);
    void put(const Header& h, const EntryView& line, const WhereView& at);

private:
    Statement entry_;
    Statement body_;
    Statement head_;
};

// import's steps around its lines: BEGIN IMMEDIATE, the deferral where
// asked and the count (StoreNotEmpty); then, once every line is in, the
// final view over pages, the heads compared, meta written, the counts.
void begin_import(Connection& c, bool defer, const WhereView& at);
// The final view's rows, in the order inserted, each with whether its
// target is held and whether a forget names it; the heads the rows give
// into heads; meta written from the rows, the counts.
inline constexpr std::string_view final_rows =
    "SELECT e.id, e.line, b.record, "
    "e.target IS NULL OR EXISTS (SELECT 1 FROM entry AS t WHERE t.id = e.target), "
    "EXISTS (SELECT 1 FROM entry AS f WHERE f.target = e.id AND f.kind = 'forget') "
    "FROM entry AS e LEFT JOIN body AS b ON b.id = e.id ORDER BY e.rowid";
// Both on one statement, the heads' kept open until meta's takes its place.
void read_heads(Statement& s, Vector<LaneHead>& heads, const WhereView& at);
[[nodiscard]] Imported write_meta(Connection& c, Statement& s, const WhereView& at, const Limits& limits);

// Once every line is in: each entry through the admit hook's settled in
// the final view over pages, afresh, then the store's settles, then the
// heads through its heads.
template <class A>
Imported end_import(Connection& c, std::span<const LaneHead> given, Vector<LaneHead>& heads,
                    std::span<std::byte> pages, Countdown* countdown, const WhereView& at, const Limits& limits,
                    std::size_t& peak, A& admit) {
    const At site{Operation::import_, at.path};
    {
        Statement s = c.prepare(final_rows, at);
        for (std::size_t i = 0; s.step(at); ++i) {
            memory::Arena work(pages);
            attach_to(work, countdown);
            const Chars chars(work);
            const Header h = read_header(s.text(0), s.blob(1), chars, at);
            const std::optional<std::string_view> content =
                s.null(2) ? std::nullopt : std::optional<std::string_view>(entry::body::detail::parts(s.blob(2)).content);
            ImportView view(c, chars, at);
            admit.settled(view, h, content, i, site, chars);
            stage::settles(h, s.integer(3) != 0, s.integer(4) != 0, !s.null(2), i, site);
            peak = std::max(peak, work.peak());
        }
    }
    heads.clear();
    Statement s = c.prepare(head_rows, at);
    read_heads(s, heads, at);
    admit.heads(given, std::span<const LaneHead>(heads), site);
    return write_meta(c, s, at, limits);
}
// The checkpoint that empties the -wal after an import, refused or not;
// its result ignored.
void truncate_wal(Connection& c) noexcept;

// import: the source's heads on the first quarter of pages, then each
// line, on the second, and its work, on the half left, each afresh.
template <platform::Platform P, Source R, class H, class A>
Imported import_source(P& platform, Connection& c, R& source, Vector<LaneHead>& heads, std::span<std::byte> pages,
                       Countdown* countdown, const WhereView& where, const Limits& limits, H& hooks,
                       std::size_t& peak, A& admit) {
    const stage::AtView at{Operation::import_, where.path};
    const std::size_t quarter = pages.size() / 4;
    memory::Arena kept(pages.first(quarter));
    attach_to(kept, countdown);
    Vector<LaneHead> given{Chars(kept)};
    source.heads(given);
    std::size_t line_peak = 0;
    std::size_t work_peak = 0;
    Imported done;
    bool begun = false;
    try {
        begin_import(c, defers<H>(), where);
        begun = true;
        hooks.begun();
        ImportRows rows(c, where);
        for (std::size_t i = 0;; ++i) {
            memory::Arena text(pages.subspan(quarter, quarter));
            attach_to(text, countdown);
            Line line(text);
            const bool more = source.next(line);
            line_peak = std::max(line_peak, text.peak());
            if (!more) {
                break;
            }
            memory::Arena work(pages.subspan(2 * quarter));
            attach_to(work, countdown);
            const EntryView v{line.header, line.has_body ? std::optional<std::string_view>(line.body) : std::nullopt};
            const stage::Entry<P> e = stage::detail::part1(platform, at, i, v, Chars(work));
            ImportView view(c, Chars(work), where);
            stage::arrives(view, e.header(), i, at);
            admit.arrived(view, e.header(), i, at);
            rows.put(e.header(), v, where);
            work_peak = std::max(work_peak, work.peak());
        }
        done = end_import(c, given, heads, pages.subspan(2 * quarter), countdown, where, limits, work_peak, admit);
        hooks.written();
        c.exec("COMMIT", where);
    } catch (...) {
        c.rollback();
        // A refusal at the begin (StoreNotEmpty, busy) spilled no page: a
        // checkpoint there would move committed frames into the file.
        if (begun) {
            truncate_wal(c);
        }
        heads.clear();
        throw;
    }
    hooks.committed();
    truncate_wal(c);
    peak = kept.peak() + line_peak + work_peak;
    return done;
}

// verify's walk: each head row's lane, its entries in seq order, each row
// checked against the one before it and its own columns; the head row its
// last entry; then the totals, which give the counts.
class WalkView {
public:
    WalkView(Connection& c, const WhereView& at);
    // The next entry's header into out, on out's allocator, checked; false
    // past the last. No optional of a Header: its allocator has no default.
    [[nodiscard]] bool next(Header& out);
    // The entry in hand: its line and its body record or none.
    [[nodiscard]] EntryView entry() const noexcept;
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    // The entry rows, the body rows and meta against the walk: the counts.
    [[nodiscard]] Counts totals();

private:
    bool lane();
    void lane_end();

    Connection* c_;
    WhereView at_;
    Statement lanes_;
    Statement rows_;
    Counts counts_;
    std::uint64_t size_ = 0;
    std::int64_t seq_ = 0;  // the last entry's in the lane in hand
    Id last_;
    std::size_t index_ = 0;
    bool in_lane_ = false;
};

// A held body record against its header: one the parser refuses
// StoreCorrupt incoherent, one not its header's body_mismatch.
template <platform::Platform P>
void check_record(P& platform, std::string_view record, const Header& h, const WhereView& at, const Chars& chars) {
    namespace body = entry::body;
    const std::string_view id = h.value(entry::header::Slot::id);
    try {
        body::check(body::parse(platform, record, chars), h);
    } catch (const body::BodyError& f) {
        const bool mismatch = f.code == body::Code::body_mismatch;
        throw StoreError::corrupt(at.operation, at.path, mismatch ? Corrupt::body_mismatch : Corrupt::incoherent, id);
    } catch (const ParseError&) {
        throw StoreError::corrupt(at.operation, at.path, Corrupt::incoherent, id);
    }
}

// The engine's own full check: a first row other than "ok" StoreCorrupt
// corrupt, naming it.
void integrity(Connection& c, const WhereView& at);

// verify, in one read transaction, each entry's work on a fresh arena over
// pages.
template <platform::Platform P>
Counts verify_store(P& platform, Connection& c, std::span<std::byte> pages, Countdown* countdown,
                    const WhereView& at, std::size_t& peak) {
    Counts counts;
    std::size_t mark = 0;
    c.exec("BEGIN", at);
    try {
        integrity(c, at);
        WalkView walk(c, at);
        for (;;) {
            memory::Arena arena(pages);
            attach_to(arena, countdown);
            const Chars chars(arena);
            Header h(chars);
            if (!walk.next(h)) {
                break;
            }
            const EntryView e = walk.entry();
            if (e.body) {
                check_record(platform, *e.body, h, at, chars);
            }
            try {
                (void)stage::detail::part1(platform, stage::AtView{Operation::verify, at.path}, walk.index(), e, chars);
            } catch (const StoreError& f) {
                if (f.kind() != Kind::entry_invalid) {
                    throw;
                }
                throw StoreError::corrupt(at.operation, at.path, Corrupt::incoherent, f.entry_code());
            }
            mark = std::max(mark, arena.peak());
        }
        counts = walk.totals();
        c.exec("COMMIT", at);
    } catch (...) {
        c.rollback();
        throw;
    }
    peak = mark;
    return counts;
}

#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Exports) == 8, "an export list's size is pinned: its first export");
static_assert(sizeof(ImportRows) == 48, "import's statements' size is pinned: three statements");
#endif
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(Reader) == 664, "an export's engine half's size is pinned: its links, connection, cursors, "
                                     "snapshot, counts, gate, flags and path in place");
#endif

}  // namespace qiven3::store::sqlite
