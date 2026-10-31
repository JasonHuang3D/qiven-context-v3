// sqlite: the writes of append and forget, through the connection wrapper
// (connection.hpp): part 2's view of the transaction, the head's check, forget's
// guard, whole records and erasure, the inserts, the head moved, meta added
// to, and size's figures. This file names nothing of the engine's own.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "store/codes.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"

namespace qiven3::store::sqlite {

namespace {

constexpr int done_code = 101;

[[noreturn]] void incoherent(const WhereView& at, std::string_view detail) {
    throw StoreError::corrupt(at.operation, at.path, Corrupt::incoherent, detail);
}

// An insert's constraint on a lane: HeadConflict, the head read again.
[[noreturn]] void conflict(Connection& c, int code, std::string_view scope, std::string_view lane,
                           Expected expected, const WhereView& at) {
    const std::optional<Head> now = head_of(c, scope, lane, at);
    WhereView w = at;
    w.place = Place::insert;
    w.scope = scope;
    w.lane = lane;
    w.expected = expected.value_or(std::string_view());
    w.actual = now ? now->id.view() : std::string_view();
    c.raise(code, w);
}

// A step of an insert, its failure mapped: a key's constraint HeadConflict.
void inserted(Connection& c, Statement& s, std::string_view scope, std::string_view lane, Expected expected,
              const WhereView& at) {
    const int code = s.step_code();
    if (code == primarykey_code || code == unique_code) {
        conflict(c, code, scope, lane, expected, at);
    }
    if (code != done_code) {
        c.raise(code, at);
    }
    s.reset();
}

}  // namespace

memory::Arena& ScratchView::operator()() {
    if (arena_) {
        peak_ = std::max(peak_, arena_->peak());
    }
    arena_.emplace(pages_);  // the last one ended first
#if defined(QIVEN3_FAULTS)
    if (countdown_ != nullptr) {
        arena_.value().attach(countdown_);
    }
#endif
    return arena_.value();
}

std::size_t ScratchView::peak() const noexcept { return std::max(peak_, arena_ ? arena_->peak() : 0); }

Totals totals(Connection& c, const WhereView& at) {
    Statement s = c.prepare(
        "SELECT (SELECT value FROM meta WHERE name = 'size'), (SELECT value FROM meta WHERE name = 'count')", at);
    if (!s.step(at) || s.null(0) || s.null(1)) {
        incoherent(at, "meta: a row missing");
    }
    if (s.integer(0) < 0 || s.integer(1) < 0) {
        incoherent(at, "meta: a size or count below 0");
    }
    return Totals{s.integer(0), s.integer(1)};
}

std::optional<Head> WriteView::head(std::string_view scope, std::string_view lane) {
    return head_of(*c_, scope, lane, at_);
}

bool WriteView::header(std::string_view id, String& out) {
    return find(*c_, id, &out, nullptr, at_) != Found::absent;
}

bool WriteView::body(std::string_view id, String& out) { return find(*c_, id, nullptr, &out, at_) == Found::body; }

bool WriteView::forgotten(std::string_view id) {
    Statement s = c_->prepare("SELECT EXISTS (SELECT 1 FROM entry WHERE target = ?1 AND kind = 'forget')", at_);
    s.bind_text(1, id, at_);
    return s.step(at_) && s.integer(0) != 0;
}

bool WriteView::line(std::string_view id, String& out) {
    Statement s = c_->prepare(held_line, at_);
    s.bind_text(1, id, at_);
    if (!s.step(at_)) {
        return false;
    }
    out.assign(s.blob(0));
    return true;
}

bool WriteView::held(std::string_view id) { return find(*c_, id, nullptr, nullptr, at_) != Found::absent; }

bool WriteView::held_in(std::string_view id, std::string_view scope) {
    Statement s = c_->prepare(held_scope, at_);
    s.bind_text(1, id, at_);
    s.bind_text(2, scope, at_);
    return s.step(at_) && s.integer(0) != 0;
}

void WriteView::targets(std::string_view id, std::string_view scope, Vector<Id>& out) {
    out.clear();
    Statement s = c_->prepare(record_members, at_);
    s.bind_text(1, id, at_);
    s.bind_text(2, scope, at_);
    while (s.step(at_)) {
        out.push_back(Id::of(s.text(0)));
    }
}

void WriteView::targets(std::string_view id, Vector<Id>& out) {
    out.clear();
    Statement s = c_->prepare(target_members, at_);
    s.bind_text(1, id, at_);
    while (s.step(at_)) {
        out.push_back(Id::of(s.text(0)));
    }
}

void WriteView::keyed(std::string_view scope, std::string_view kind, Vector<Id>& out) {
    out.clear();
    Statement s = c_->prepare(keyed_roots, at_);
    s.bind_text(1, scope, at_);
    s.bind_text(2, kind, at_);
    while (s.step(at_)) {
        out.push_back(Id::of(s.text(0)));
    }
}

bool WriteView::contains(std::string_view bytes) {
    Statement s = c_->prepare(content_scan, at_);
    s.bind_blob(1, bytes, at_);
    return s.step(at_) && s.integer(0) != 0;
}

std::optional<Head> expect(Connection& c, std::string_view scope, std::string_view lane, Expected expected,
                           const WhereView& at) {
    const std::optional<Head> now = head_of(c, scope, lane, at);
    const std::string_view actual = now ? now->id.view() : std::string_view();
    if (expected.has_value() != now.has_value() || expected.value_or(std::string_view()) != actual) {
        throw StoreError::head_conflict(at.operation, at.path, scope, lane, expected.value_or(std::string_view()),
                                        actual);
    }
    return now;
}

void guard(Connection& c, std::span<const GuardView> guard, const WhereView& at) {
    {
        Statement s = c.prepare("SELECT scope, lane, id FROM head", at);
        while (s.step(at)) {
            const std::string_view scope = s.text(0);
            const std::string_view lane = s.text(1);
            const std::string_view id = s.text(2);
            const auto g = std::find_if(guard.begin(), guard.end(),
                                        [&](const GuardView& v) { return v.scope == scope && v.lane == lane; });
            const bool absent = g == guard.end();
            if (absent || g->id != id) {
                throw StoreError::head_conflict(at.operation, at.path, scope, lane,
                                                absent ? std::string_view() : g->id, id);
            }
        }
    }
    for (const GuardView& v : guard) {
        if (!head_of(c, v.scope, v.lane, at)) {
            throw StoreError::head_conflict(at.operation, at.path, v.scope, v.lane, v.id, {});
        }
    }
}

std::uint64_t erase(Connection& c, std::span<const Id> targets, std::string_view scope, Vector<Id>& erased,
                    const WhereView& at) {
    erased.clear();
    erased.reserve(targets.size());
    std::uint64_t bytes = 0;
    Statement s = c.prepare(
        "DELETE FROM body WHERE id = ?1 AND EXISTS (SELECT 1 FROM entry e WHERE e.id = ?1 AND e.scope = ?2) "
        "RETURNING length(record)",
        at);
    for (std::size_t j = 0; j < targets.size(); ++j) {
        const std::span<const Id> before = targets.first(j);
        if (std::find(before.begin(), before.end(), targets[j]) != before.end()) {
            continue;
        }
        s.bind_text(1, targets[j].view(), at);
        s.bind_text(2, scope, at);
        if (s.step(at)) {
            bytes += static_cast<std::uint64_t>(s.integer(0)) + 1U;
            erased.push_back(targets[j]);
            while (s.step(at)) {
            }
        }
        s.reset();
    }
    return bytes;
}

void insert(Connection& c, std::span<const RowView> rows, Expected expected, const WhereView& at) {
    {
        Statement s = c.prepare("INSERT INTO entry (id, scope, lane, seq, parent, kind, target, line, body) VALUES "
                                "(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)",
                                at);
        for (const RowView& r : rows) {
            s.bind_text(1, r.id, at);
            s.bind_text(2, r.scope, at);
            s.bind_text(3, r.lane, at);
            s.bind(4, r.seq, at);
            r.parent ? s.bind_text(5, *r.parent, at) : s.bind_null(5, at);
            s.bind_text(6, r.kind, at);
            r.target ? s.bind_text(7, *r.target, at) : s.bind_null(7, at);
            s.bind_blob(8, r.line, at);
            s.bind(9, r.body ? 1 : 0, at);
            inserted(c, s, r.scope, r.lane, expected, at);
        }
    }
    Statement s = c.prepare("INSERT INTO body (id, record) VALUES (?1, ?2)", at);
    for (const RowView& r : rows) {
        if (r.record) {
            s.bind_text(1, r.id, at);
            s.bind_blob(2, *r.record, at);
            inserted(c, s, r.scope, r.lane, expected, at);
        }
    }
}

void advance(Connection& c, std::string_view scope, std::string_view lane, Expected expected, const Head& head,
             const WhereView& at) {
    if (!expected) {
        Statement s = c.prepare("INSERT INTO head (scope, lane, id, seq) VALUES (?1, ?2, ?3, ?4)", at);
        s.bind_text(1, scope, at);
        s.bind_text(2, lane, at);
        s.bind_text(3, head.id.view(), at);
        s.bind(4, head.seq, at);
        inserted(c, s, scope, lane, expected, at);
        return;
    }
    Statement s = c.prepare("UPDATE head SET id = ?1, seq = ?2 WHERE scope = ?3 AND lane = ?4 AND id = ?5", at);
    s.bind_text(1, head.id.view(), at);
    s.bind(2, head.seq, at);
    s.bind_text(3, scope, at);
    s.bind_text(4, lane, at);
    s.bind_text(5, *expected, at);
    static_cast<void>(s.step(at));
    if (c.changes() != 1) {
        const std::optional<Head> now = head_of(c, scope, lane, at);
        throw StoreError::head_conflict(at.operation, at.path, scope, lane, *expected,
                                        now ? now->id.view() : std::string_view());
    }
}

void add_meta(Connection& c, std::int64_t size, std::int64_t count, std::int64_t forgets, const WhereView& at) {
    Statement s = c.prepare("UPDATE meta SET value = value + CASE name WHEN 'size' THEN ?1 WHEN 'count' THEN ?2 "
                            "ELSE ?3 END WHERE name IN ('size', 'count', 'forgets')",
                            at);
    s.bind(1, size, at);
    s.bind(2, count, at);
    s.bind(3, forgets, at);
    static_cast<void>(s.step(at));
    if (c.changes() != 3) {
        incoherent(at, "meta: a row missing");
    }
}

std::string_view raise_meta(std::string_view name, std::int64_t value, std::array<char, 96>& out) noexcept {
    std::size_t n = 0;
    const auto add = [&out, &n](std::string_view part) {
        for (const char ch : part) {
            if (n < out.size()) {
                out.at(n++) = ch;
            }
        }
    };
    std::array<char, 24> digits{};
    std::size_t d = digits.size();
    auto magnitude = static_cast<std::uint64_t>(std::max<std::int64_t>(value, 0));
    do {
        digits.at(--d) = static_cast<char>('0' + (magnitude % 10U));
        magnitude /= 10U;
    } while (magnitude != 0U && d > 0);
    add("UPDATE meta SET value = max(value, ");
    add(std::string_view(digits.data(), digits.size()).substr(d));
    add(") WHERE name = '");
    add(name);
    add("'");
    return {out.data(), n};
}

void figures(Connection& c, Sizes& out, const WhereView& at) {
    const auto add = [&out](std::string_view name, std::uint64_t value) {
        out.detail.at(out.figures) = Figure{memory::Inline<figure_bytes>(name), value};
        ++out.figures;
    };
    add("page_size", static_cast<std::uint64_t>(c.integer("PRAGMA page_size", at)));
    add("page_count", static_cast<std::uint64_t>(c.integer("PRAGMA page_count", at)));
    add("freelist_count", static_cast<std::uint64_t>(c.integer("PRAGMA freelist_count", at)));
    const std::string_view file = c.filename();
    add("file_bytes", file_bytes(file).value_or(0));
    add("wal_bytes", file_bytes(file, "-wal").value_or(0));
    add("shm_bytes", file_bytes(file, "-shm").value_or(0));
}

}  // namespace qiven3::store::sqlite
