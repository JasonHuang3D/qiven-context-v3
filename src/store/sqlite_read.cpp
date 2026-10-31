// sqlite: the reads, through the connection wrapper (connection.hpp): a lane's
// head, heads in one read transaction, an entry's line and body record by
// id, forgotten, and the scan's cursor over the keys' columns and the line,
// ending at each lane's length at its start. This file names nothing of the
// engine's own.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "keys/keys.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/engine/sqlite.hpp"
#include "store/errors.hpp"

namespace qiven3::store::sqlite {

namespace {

[[noreturn]] void incoherent(const WhereView& at, std::string_view detail) {
    throw StoreError::corrupt(at.operation, at.path, Corrupt::incoherent, detail);
}

// The columns find and a scan select, in this order.
constexpr int line_column = 7;
constexpr int body_column = 8;
constexpr int denied_column = 9;

}  // namespace

std::optional<Head> head_of(Connection& c, std::string_view scope, std::string_view lane, const WhereView& at) {
    Statement s = c.prepare("SELECT id, seq FROM head WHERE scope = ?1 AND lane = ?2", at);
    s.bind_text(1, scope, at);
    s.bind_text(2, lane, at);
    if (!s.step(at)) {
        return std::nullopt;
    }
    return Head{Id::of(s.text(0)), s.integer(1)};
}

HeadsRead heads(Connection& c, const Scopes& scopes, Vector<LaneHead>& rows, const WhereView& at) {
    rows.clear();
    HeadsRead read;
    c.exec("BEGIN", at);
    try {
        Statement s = c.prepare("SELECT scope, lane, id, seq FROM head ORDER BY scope, lane", at);
        while (s.step(at)) {
            if (in_scopes(scopes, s.text(0))) {
                rows.push_back(LaneHead{Head{Id::of(s.text(2)), s.integer(3)}, memory::Inline<scope_bytes>(s.text(0)),
                                        memory::Inline<lane_bytes>(s.text(1))});
            }
        }
        s = c.prepare("SELECT scope, count(*) FROM entry WHERE kind = 'forget' GROUP BY scope", at);
        while (s.step(at)) {
            read.forgets += in_scopes(scopes, s.text(0)) ? static_cast<std::uint64_t>(s.integer(1)) : 0U;
        }
        s = c.prepare("SELECT (SELECT value FROM meta WHERE name = 'forgets') > "
                      "(SELECT value FROM meta WHERE name = 'purged')",
                      at);
        read.pending = s.step(at) && s.integer(0) != 0;
        s = Statement();
        c.exec("COMMIT", at);
    } catch (...) {
        c.rollback();
        throw;
    }
    return read;
}

Found find(Connection& c, std::string_view id, memory::String* line, memory::String* record, const WhereView& at) {
    Statement s = c.prepare("SELECT e.line, b.record, e.body FROM entry AS e LEFT JOIN body AS b ON b.id = e.id "
                            "WHERE e.id = ?1",
                            at);
    s.bind_text(1, id, at);
    if (!s.step(at)) {
        return Found::absent;
    }
    if (line != nullptr) {
        line->assign(s.blob(0));
    }
    if (s.null(1)) {
        // A body key with no row: lost, no forget standing for the row.
        if (record != nullptr && s.integer(2) != 0 && !forgotten(c, id, at)) {
            incoherent(at, id);
        }
        return Found::entry;
    }
    if (record != nullptr) {
        record->assign(s.blob(1));
    }
    return Found::body;
}

bool forgotten(Connection& c, std::string_view id, const WhereView& at) {
    Statement s = c.prepare("SELECT EXISTS (SELECT 1 FROM entry WHERE target = e.id AND kind = 'forget') "
                            "FROM entry AS e WHERE e.id = ?1",
                            at);
    s.bind_text(1, id, at);
    if (!s.step(at)) {
        throw StoreError::not_found(at.operation, at.path, id);
    }
    return s.integer(0) != 0;
}

ScanView::ScanView(const Gate& gate, Connection& c, std::span<std::byte> pages, Countdown* countdown,
                   std::string_view path, const RangeView& range)
    : gate_(&gate), c_(&c), pages_(pages), countdown_(countdown), path_(path),
      after_(std::max<std::int64_t>(range.after.value_or(0), 0)),
      // Each lane ends at its length now: no row past the greatest rowid.
      last_(c.integer("SELECT coalesce(max(rowid), 0) FROM entry", site(name(Operation::scan), path))),
      // A scope or lane past what a row can hold matches none.
      done_((range.scope && range.scope->size() > scope_bytes) || (range.lane && range.lane->size() > lane_bytes)) {
    if (range.scope) {
        scope_.emplace(*range.scope);
        at_scope_ = memory::Inline<scope_bytes>(*range.scope);
    }
    if (range.lane) {
        lane_.emplace(*range.lane);
    }
    if (range.scope && range.lane) {
        at_lane_ = memory::Inline<lane_bytes>(*range.lane);
    }
}

bool ScanView::fetch(Record& out) {
    gate_->enter(Operation::scan, path_);
    if (done_) {
        return false;
    }
    const WhereView at = site(name(Operation::scan), path_);
    // Its body key, then whether a body row lost or stray denies it, no
    // forget standing for the row.
    Statement s = c_->prepare("SELECT e.id, e.scope, e.lane, e.seq, e.parent, e.kind, e.target, e.line, e.body, "
                              "(e.body <> 0) <> EXISTS (SELECT 1 FROM body AS b WHERE b.id = e.id) AND NOT "
                              "EXISTS (SELECT 1 FROM entry AS f WHERE f.target = e.id AND f.kind = 'forget') "
                              "FROM entry AS e WHERE (e.scope, e.lane, e.seq) > (?1, ?2, ?3) "
                              "AND (?4 IS NULL OR e.scope = ?4) AND (?5 IS NULL OR e.lane = ?5) AND e.seq > ?6 "
                              "AND e.rowid <= ?7 ORDER BY e.scope, e.lane, e.seq LIMIT 1",
                              at);
    s.bind_text(1, at_scope_.view(), at);
    s.bind_text(2, at_lane_.view(), at);
    s.bind(3, at_seq_, at);
    if (scope_) {
        s.bind_text(4, scope_->view(), at);
    } else {
        s.bind_null(4, at);
    }
    if (lane_) {
        s.bind_text(5, lane_->view(), at);
    } else {
        s.bind_null(5, at);
    }
    s.bind(6, after_, at);
    s.bind(7, last_, at);
    if (!s.step(at)) {
        done_ = true;
        return false;
    }
    if (s.integer(denied_column) != 0) {
        incoherent(at, s.text(0));
    }
    const auto id = [&s](int column) {
        return s.null(column) ? std::nullopt : std::optional<Id>(Id::of(s.text(column)));
    };
    out.kind.assign(s.text(5));
    out.line.assign(s.blob(line_column));
    out.id = Id::of(s.text(0));
    out.scope = memory::Inline<scope_bytes>(s.text(1));
    out.lane = memory::Inline<lane_bytes>(s.text(2));
    out.seq = s.integer(3);
    out.parent = id(4);
    out.target = id(6);
    out.body = s.integer(body_column) != 0;
    return true;
}

}  // namespace qiven3::store::sqlite
