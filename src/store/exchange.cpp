// exchange: the SQLite store's export, import and verify (exchange.hpp),
// through the connection wrapper. This file names nothing of the engine's
// own: every call is the wrapper's.

#include "store/exchange.hpp"

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
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"
#include "store/keys.hpp"
#include "store/stage.hpp"

namespace qiven3::store::sqlite {

namespace {

using entry::header::Slot;

[[noreturn]] void incoherent(const WhereView& at, std::string_view detail) {
    throw StoreError::corrupt(at.operation, at.path, Corrupt::incoherent, detail);
}

constexpr int line_column = 7;
constexpr int record_column = 8;
constexpr int forgotten_column = 9;
constexpr int held_column = 10;

// The packed records at an export's pages' front: a text's length in a
// byte, unheld for one past its bound, which then names nothing, then its
// bytes; a seq in eight bytes, the low first. Each scope of the set, then
// each seq to start after with its scope and lane. A set's entry ending in
// `*` is packed as its bytes before the `*`, its length byte's high bit
// set: so an entry one past the bound that ends in `*` is held too, and
// one longer names no scope the store holds.
constexpr std::size_t seq_bytes = 8;
constexpr std::size_t scope_record = 1 + scope_bytes;
constexpr std::size_t lane_record = 1 + lane_bytes;
constexpr std::size_t after_record = scope_record + lane_record + seq_bytes;
constexpr std::byte unheld{0xFF};

void pack_text(std::span<std::byte> field, std::string_view text, std::size_t bound) noexcept {
    if (text.size() > bound) {
        field.front() = unheld;
        return;
    }
    field.front() = static_cast<std::byte>(text.size());
    std::ranges::transform(text, field.subspan(1).begin(), [](char c) { return static_cast<std::byte>(c); });
}

constexpr std::byte prefix_bit{0x80};

void pack_entry(std::span<std::byte> field, std::string_view entry) noexcept {
    const bool prefix = entry.ends_with('*');
    pack_text(field, prefix ? entry.substr(0, entry.size() - 1) : entry, scope_bytes);
    if (prefix && field.front() != unheld) {
        field.front() |= prefix_bit;
    }
}

// The set's entry a packed field holds, its bytes in buffer; none if
// unheld.
std::optional<std::string_view> entry_of(std::span<const std::byte> field,
                                         std::array<char, scope_bytes + 1>& buffer) noexcept {
    if (field.front() == unheld) {
        return std::nullopt;
    }
    const bool prefix = (field.front() & prefix_bit) != std::byte{0};
    const std::size_t n = std::to_integer<std::size_t>(field.front() & ~prefix_bit);
    std::ranges::transform(field.subspan(1, n), buffer.begin(), [](std::byte b) { return static_cast<char>(b); });
    if (prefix) {
        buffer.at(n) = '*';
    }
    return std::string_view(buffer.data(), prefix ? n + 1 : n);
}

bool field_is(std::span<const std::byte> field, std::string_view text) noexcept {
    if (field.front() == unheld || std::to_integer<std::size_t>(field.front()) != text.size()) {
        return false;
    }
    return std::ranges::equal(field.subspan(1, text.size()), text,
                              [](std::byte b, char c) { return b == static_cast<std::byte>(c); });
}

void pack_seq(std::span<std::byte> field, std::int64_t seq) noexcept {
    const auto u = static_cast<std::uint64_t>(seq);
    for (std::size_t i = 0; i < seq_bytes; ++i) {
        field[i] = static_cast<std::byte>((u >> (8U * i)) & 0xFFU);
    }
}

std::int64_t seq_of(std::span<const std::byte> field) noexcept {
    std::uint64_t u = 0;
    for (std::size_t i = 0; i < seq_bytes; ++i) {
        u |= std::to_integer<std::uint64_t>(field[i]) << (8U * i);
    }
    return static_cast<std::int64_t>(u);
}

// The work after the packed records.
template <class B>
std::span<B> work_of(std::span<B> pages, std::size_t scopes, std::size_t afters) noexcept {
    return pages.subspan((scopes * scope_record) + (afters * after_record));
}

LaneHead lane_head(const Statement& s) {
    return LaneHead{Head{Id::of(s.text(2)), s.integer(3)}, memory::Inline<scope_bytes>(s.text(0)),
                    memory::Inline<lane_bytes>(s.text(1))};
}

// A statement that returns no row run, then reset for its next.
void run(Statement& s, const WhereView& at) {
    static_cast<void>(s.step(at));
    s.reset();
}

void bind_optional(Statement& s, int index, std::optional<std::string_view> text, const WhereView& at) {
    if (text) {
        s.bind_text(index, *text, at);
    } else {
        s.bind_null(index, at);
    }
}

}  // namespace

// The export's engine half.

std::size_t Reader::pages_for(const Scopes& scopes, std::span<const AfterView> after) {
    const std::size_t n = scopes ? scopes->size() : 0;
    const std::optional<std::size_t> set = memory::mul(n, scope_record);
    const std::optional<std::size_t> seqs = memory::mul(after.size(), after_record);
    const std::optional<std::size_t> packed = set && seqs ? memory::add(*set, *seqs) : std::nullopt;
    const std::optional<std::size_t> all = packed ? memory::add(*packed, operation_base) : std::nullopt;
    if (!all) {
        throw memory::MemoryError::budget("store/exchange", std::numeric_limits<std::size_t>::max(), after.size(), 0);
    }
    return *all;
}

void Reader::open(Exports& owner, std::span<std::byte> pages, std::string_view file, std::string_view path,
                  const Limits& limits, const Scopes& scopes, std::span<const AfterView> after, Countdown* countdown) {
    owner_ = &owner;
    next_ = owner.first_;
    if (next_ != nullptr) {
        next_->prev_ = this;
    }
    owner.first_ = this;
    countdown_ = countdown;
    path_ = memory::Inline<path_bytes>(path);
    every_ = !scopes;
    if (scopes) {
        for (const std::string_view s : *scopes) {
            pack_entry(pages.subspan(scopes_ * scope_record, scope_record), s);
            ++scopes_;
        }
    }
    const std::span<std::byte> seqs = pages.subspan(scopes_ * scope_record);
    for (const AfterView& a : after) {
        const std::span<std::byte> r = seqs.subspan(afters_ * after_record, after_record);
        pack_text(r.first(scope_record), a.scope, scope_bytes);
        pack_text(r.subspan(scope_record, lane_record), a.lane, lane_bytes);
        pack_seq(r.last(seq_bytes), a.seq);
        ++afters_;
    }
    const WhereView at = site();
    {
        memory::Arena arena(work_of(pages, scopes_, afters_));
        attach_to(arena, countdown_);
        String name{Chars(arena)};
        name.assign(file);
        c_ = Connection::open(name, true, at);
        peak_ = arena.peak();
    }
    identify(c_, at);
    apply(c_, true, at);
    c_.exec("BEGIN", at);
    const Totals t = totals(c_, at);  // the first read: the snapshot
    Statement s = c_.prepare(forget_counts, at);
    while (s.step(at)) {
        snapshot_.forgets += in_set(pages, s.text(0)) ? static_cast<std::uint64_t>(s.integer(1)) : 0U;
    }
    snapshot_.volume = measure(static_cast<std::uint64_t>(t.size), static_cast<std::uint64_t>(t.count), limits);
    lanes_ = c_.prepare(head_rows, at);
    rows_ = c_.prepare(lane_rows, at);
}

Snapshot Reader::heads(std::span<const std::byte> pages, Vector<LaneHead>& rows) {
    gate_.enter(Operation::export_, path_.view());
    const WhereView at = site();
    rows.clear();
    Statement s = c_.prepare(head_rows, at);
    while (s.step(at)) {
        if (in_set(pages, s.text(0))) {
            rows.push_back(lane_head(s));
        }
    }
    return snapshot_;
}

bool Reader::next(std::span<std::byte> pages, Line& line) {
    gate_.enter(Operation::export_, path_.view());
    const WhereView at = site();
    while (!done_) {
        if (in_lane_ && rows_.step(at)) {
            memory::Arena arena(work_of(pages, scopes_, afters_));
            attach_to(arena, countdown_);
            const Header h = row_header(rows_, Chars(arena), at);
            const bool forgotten = rows_.integer(forgotten_column) != 0;
            const bool body = !forgotten && !rows_.null(record_column);
            if (!body && !forgotten && h.text(Slot::content)) {
                incoherent(at, h.value(Slot::id));
            }
            const auto text = [this](int column) {
                return rows_.null(column) ? std::nullopt : std::optional<std::string_view>(rows_.text(column));
            };
            // Its body key: a body row, or a forget of it, as a scan's.
            const bool keyed = forgotten || !rows_.null(record_column);
            const KeysView keys{rows_.text(0), rows_.text(1), rows_.text(2), rows_.text(5),
                                rows_.integer(3), text(4), text(6), keyed};
            if (!line.keys.assign(keys)) {
                incoherent(at, keys.id);
            }
            line.header.assign(rows_.blob(line_column));
            line.body.assign(body ? rows_.blob(record_column) : std::string_view());
            line.has_body = body;
            line.forgotten = forgotten;
            peak_ = std::max(peak_, arena.peak());
            return true;
        }
        in_lane_ = lane(pages, at);
        done_ = !in_lane_;
    }
    return false;
}

bool Reader::lane(std::span<const std::byte> pages, const WhereView& at) {
    rows_.reset();
    while (lanes_.step(at)) {
        // The head row in hand's views, bound until lanes_ steps again.
        const std::string_view scope = lanes_.text(0);
        if (!in_set(pages, scope)) {
            continue;
        }
        const std::string_view lane = lanes_.text(1);
        rows_.bind_text(1, scope, at);
        rows_.bind_text(2, lane, at);
        rows_.bind(3, after_of(pages, scope, lane), at);
        return true;
    }
    return false;
}

void Reader::close() noexcept {
    gate_.close();
    rows_ = Statement();
    lanes_ = Statement();
    c_.rollback();
    c_.close();
    unlink();
}

Reader::Reader(Reader&& other) noexcept { take(other); }

Reader& Reader::operator=(Reader&& other) noexcept {
    if (this != &other) {
        close();
        take(other);
    }
    return *this;
}

void Reader::take(Reader& other) noexcept {
    c_ = std::move(other.c_);
    lanes_ = std::move(other.lanes_);
    rows_ = std::move(other.rows_);
    snapshot_ = other.snapshot_;
    countdown_ = other.countdown_;
    scopes_ = other.scopes_;
    afters_ = other.afters_;
    peak_ = other.peak_;
    gate_ = other.gate_;
    every_ = other.every_;
    in_lane_ = other.in_lane_;
    done_ = other.done_;
    path_ = other.path_;
    owner_ = other.owner_;
    prev_ = other.prev_;
    next_ = other.next_;
    if (owner_ != nullptr) {
        (prev_ != nullptr ? prev_->next_ : owner_->first_) = this;
        if (next_ != nullptr) {
            next_->prev_ = this;
        }
    }
    other.owner_ = nullptr;
    other.prev_ = nullptr;
    other.next_ = nullptr;
    other.gate_.close();
}

void Reader::unlink() noexcept {
    if (owner_ == nullptr) {
        return;
    }
    (prev_ != nullptr ? prev_->next_ : owner_->first_) = next_;
    if (next_ != nullptr) {
        next_->prev_ = prev_;
    }
    owner_ = nullptr;
    prev_ = nullptr;
    next_ = nullptr;
}

bool Reader::in_set(std::span<const std::byte> pages, std::string_view scope) const {
    if (every_) {
        return true;
    }
    std::array<char, scope_bytes + 1> buffer{};
    for (std::size_t i = 0; i < scopes_; ++i) {
        const std::optional<std::string_view> entry = entry_of(pages.subspan(i * scope_record, scope_record), buffer);
        if (entry && in_scope(*entry, scope)) {
            return true;
        }
    }
    return false;
}

std::int64_t Reader::after_of(std::span<const std::byte> pages, std::string_view scope,
                              std::string_view lane) const {
    const std::span<const std::byte> seqs = pages.subspan(scopes_ * scope_record);
    std::int64_t after = 0;
    for (std::size_t i = 0; i < afters_; ++i) {
        const std::span<const std::byte> r = seqs.subspan(i * after_record, after_record);
        if (field_is(r.first(scope_record), scope) && field_is(r.subspan(scope_record, lane_record), lane)) {
            after = seq_of(r.last(seq_bytes));
        }
    }
    return after;
}

WhereView Reader::site() const noexcept { return sqlite::site(name(Operation::export_), path_.view()); }

void Exports::close() noexcept {
    while (first_ != nullptr) {
        first_->close();
    }
}

// Import.

std::optional<Id> ImportView::forget_of(std::string_view id) {
    Statement s =
        c_->prepare("SELECT id FROM entry WHERE target = ?1 AND kind = 'forget' ORDER BY rowid LIMIT 1", at_);
    s.bind_text(1, id, at_);
    if (!s.step(at_)) {
        return std::nullopt;
    }
    return Id::of(s.text(0));
}

ImportRows::ImportRows(Connection& c, const WhereView& at)
    : entry_(c.prepare("INSERT INTO entry (id, scope, lane, seq, parent, kind, target, line) "
                       "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
                       at)),
      body_(c.prepare("INSERT INTO body (id, record) VALUES (?1, ?2)", at)),
      head_(c.prepare("INSERT INTO head (scope, lane, id, seq) VALUES (?1, ?2, ?3, ?4) "
                      "ON CONFLICT (scope, lane) DO UPDATE SET id = excluded.id, seq = excluded.seq",
                      at)) {}

KeysView final_keys(const Statement& s, std::size_t i, std::span<const std::uint64_t> bare) {
    const auto text = [&s](int column) {
        return s.null(column) ? std::nullopt : std::optional<std::string_view>(s.text(column));
    };
    const bool body = !s.null(2) || std::ranges::binary_search(bare, static_cast<std::uint64_t>(i));
    return KeysView{s.text(0), s.text(5), s.text(6), s.text(9), s.integer(7), text(8), text(10), body};
}

void ImportRows::put(const EntryView& line, const WhereView& at) {
    const KeysView& k = line.keys;
    const std::string_view id = k.id;
    entry_.bind_text(1, id, at);
    entry_.bind_text(2, k.scope, at);
    entry_.bind_text(3, k.lane, at);
    entry_.bind(4, k.seq, at);
    bind_optional(entry_, 5, k.parent, at);
    entry_.bind_text(6, k.kind, at);
    bind_optional(entry_, 7, k.target, at);
    entry_.bind_blob(8, line.line, at);
    run(entry_, at);
    if (line.body) {
        body_.bind_text(1, id, at);
        body_.bind_blob(2, *line.body, at);
        run(body_, at);
    }
    head_.bind_text(1, k.scope, at);
    head_.bind_text(2, k.lane, at);
    head_.bind_text(3, id, at);
    head_.bind(4, k.seq, at);
    run(head_, at);
}

void begin_import(Connection& c, bool defer, const WhereView& at) {
    c.exec("BEGIN IMMEDIATE", at);
    if (defer) {
        c.exec("PRAGMA defer_foreign_keys = ON", at);
    }
    const Totals held = totals(c, at);
    if (held.count != 0) {
        throw StoreError::not_empty(at.operation, at.path, static_cast<std::uint64_t>(held.count));
    }
}

void read_heads(Statement& s, Vector<LaneHead>& heads, const WhereView& at) {
    while (s.step(at)) {
        heads.push_back(lane_head(s));
    }
}

Imported write_meta(Connection& c, Statement& s, const WhereView& at, const Limits& limits) {
    s = c.prepare("SELECT (SELECT count(*) FROM entry), (SELECT count(*) FROM body), "
                  "(SELECT count(*) FROM entry WHERE kind = 'forget'), (SELECT count(*) FROM head), "
                  "(SELECT coalesce(sum(length(line)), 0) FROM entry), (SELECT coalesce(sum(length(record)), 0) FROM body)",
                  at);
    if (!s.step(at)) {
        incoherent(at, "the rows uncounted");
    }
    const auto u = [&s](int column) { return static_cast<std::uint64_t>(s.integer(column)); };
    const Counts n{u(0), u(1), u(2), u(3)};
    // Each line and record with its LF.
    const std::uint64_t size = stage::detail::sum(stage::detail::sum(u(4), n.entries), stage::detail::sum(u(5), n.bodies));
    s = c.prepare("UPDATE meta SET value = CASE name WHEN 'size' THEN ?1 WHEN 'count' THEN ?2 ELSE ?3 END", at);
    s.bind(1, static_cast<std::int64_t>(size), at);
    s.bind(2, static_cast<std::int64_t>(n.entries), at);
    s.bind(3, static_cast<std::int64_t>(n.forgets), at);
    run(s, at);
    if (c.changes() != 5) {
        incoherent(at, "meta: a row missing");
    }
    return Imported{n, measure(size, n.entries, limits)};
}

void truncate_wal(Connection& c) noexcept {
    c.wait(0);
    static_cast<void>(c.run("PRAGMA wal_checkpoint(TRUNCATE)"));
    c.wait(busy_milliseconds);
}

// Verify.

void integrity(Connection& c, const WhereView& at) {
    Statement s = c.prepare("PRAGMA integrity_check", at);
    if (!s.step(at)) {
        incoherent(at, "integrity_check: no row");
    }
    if (s.text(0) != "ok") {
        throw StoreError::corrupt(at.operation, at.path, Corrupt::corrupt, s.text(0));
    }
}

WalkView::WalkView(Connection& c, const WhereView& at)
    : c_(&c), at_(at), lanes_(c.prepare(head_rows, at)), rows_(c.prepare(lane_rows, at)) {}

bool WalkView::lane() {
    rows_.reset();
    if (!lanes_.step(at_)) {
        return false;
    }
    rows_.bind_text(1, lanes_.text(0), at_);
    rows_.bind_text(2, lanes_.text(1), at_);
    rows_.bind(3, 0, at_);
    ++counts_.lanes;
    index_ = 0;
    seq_ = 0;
    return true;
}

void WalkView::lane_end() {
    if (index_ == 0 || lanes_.text(2) != last_.view() || lanes_.integer(3) != seq_) {
        incoherent(at_, "head: a row not naming its lane's last entry");
    }
}

bool WalkView::next() {
    for (;;) {
        if (in_lane_ && rows_.step(at_)) {
            break;
        }
        if (in_lane_) {
            lane_end();
        }
        in_lane_ = lane();
        if (!in_lane_) {
            return false;
        }
    }
    const std::string_view id = rows_.text(0);
    const std::optional<std::string_view> parent =
        rows_.null(4) ? std::nullopt : std::optional<std::string_view>(rows_.text(4));
    const bool chained = index_ == 0 ? !parent : parent == last_.view();
    if (!Id::fits(id) || !chained || rows_.integer(3) != seq_ + 1 || rows_.integer(held_column) == 0) {
        incoherent(at_, id);
    }
    const bool forget = rows_.text(5) == "forget";
    const bool forgotten = rows_.integer(forgotten_column) != 0;
    const bool record = !rows_.null(record_column);
    if (record && (forgotten || forget)) {
        throw StoreError::corrupt(at_.operation, at_.path, Corrupt::forgotten_body, id);
    }
    ++counts_.entries;
    counts_.bodies += record ? 1U : 0U;
    counts_.forgets += forget ? 1U : 0U;
    size_ += rows_.blob(line_column).size() + 1U + (record ? rows_.blob(record_column).size() + 1U : 0U);
    last_ = Id::of(id);
    seq_ = rows_.integer(3);
    ++index_;
    return true;
}

Counts WalkView::totals() {
    Statement s = c_->prepare(
        "SELECT (SELECT count(*) FROM entry), (SELECT count(*) FROM body), "
        "(SELECT value FROM meta WHERE name = 'size'), (SELECT value FROM meta WHERE name = 'count'), "
        "(SELECT value FROM meta WHERE name = 'forgets'), (SELECT value FROM meta WHERE name = 'vacuumed'), "
        "(SELECT value FROM meta WHERE name = 'purged')",
        at_);
    if (!s.step(at_)) {
        incoherent(at_, "the rows uncounted");
    }
    const auto u = [&s](int column) { return static_cast<std::uint64_t>(s.integer(column)); };
    if (u(0) != counts_.entries) {
        incoherent(at_, "entry: a row in no lane a head row names");
    }
    if (u(1) != counts_.bodies) {
        incoherent(at_, "body: a row of no entry");
    }
    if (u(2) != size_ || u(3) != counts_.entries || u(4) != counts_.forgets) {
        incoherent(at_, "meta: the counters not the rows'");
    }
    if (s.integer(6) < 0 || s.integer(6) > s.integer(5) || s.integer(5) > s.integer(4)) {
        incoherent(at_, "meta: purged, vacuumed and forgets out of order");
    }
    return counts_;
}

}  // namespace qiven3::store::sqlite
