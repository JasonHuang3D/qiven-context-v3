// sqlite: the reads, through the connection wrapper (sqlite.hpp): a header
// line parsed and checked, a lane's head, heads in one read transaction,
// an entry by id with its columns compared with its line, forgotten, and
// the scan's cursor, ending at each lane's length at its start. This file names nothing of the engine's own.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/sqlite.hpp"

namespace qiven3::store::sqlite {

namespace {

namespace header = entry::header;
using header::Slot;

[[noreturn]] void incoherent(const WhereView& at, std::string_view detail) {
    throw StoreError::corrupt(at.operation, at.path, Corrupt::incoherent, detail);
}

// The columns find and a scan select, in this order.
constexpr int line_column = 7;
constexpr int record_column = 8;

// A row's header: its line checked against its id, then each column
// against the line's field.
Header row_header(const Statement& s, const Chars& allocator, const WhereView& at) {
    Header h = read_header(s.text(0), s.blob(line_column), allocator, at);
    const auto optional = [&s](int column) {
        return s.null(column) ? std::nullopt : std::optional<std::string_view>(s.text(column));
    };
    if (h.value(Slot::scope) != s.text(1) || h.value(Slot::lane) != s.text(2) || h.seq() != s.integer(3) ||
        h.text(Slot::parent) != optional(4) || h.value(Slot::kind) != s.text(5) ||
        h.text(Slot::target) != optional(6)) {
        incoherent(at, "an entry's column differs from its line");
    }
    return h;
}

// Whether a scope set holds scope: none, every scope; `project:*` every
// project scope.
bool in_scopes(const Scopes& scopes, std::string_view scope) noexcept {
    return !scopes || std::any_of(scopes->begin(), scopes->end(), [scope](std::string_view s) {
               return s == scope || (s == "project:*" && scope.starts_with("project:"));
           });
}

}  // namespace

Header read_header(std::string_view id, std::string_view line, const Chars& allocator, const WhereView& at) {
    Header h(allocator);
    try {
        h = header::parse_line(line, allocator);
    } catch (const ParseError&) {
        incoherent(at, "a header line the parser refuses");
    } catch (const header::HeaderError&) {
        incoherent(at, "a header line that is no object");
    }
    if (const std::optional<header::Code> fault = header::try_check(h)) {
        if (*fault != header::Code::id_mismatch) {
            incoherent(at, header::name(*fault));
        }
        throw StoreError::corrupt(at.operation, at.path, Corrupt::id_mismatch, id);
    }
    if (h.value(Slot::id) != id) {
        throw StoreError::corrupt(at.operation, at.path, Corrupt::id_mismatch, id);
    }
    return h;
}

std::optional<Head> head_of(Connection& c, std::string_view scope, std::string_view lane, const WhereView& at) {
    Statement s = c.prepare("SELECT id, seq FROM head WHERE scope = ?1 AND lane = ?2", at);
    s.bind_text(1, scope, at);
    s.bind_text(2, lane, at);
    if (!s.step(at)) {
        return std::nullopt;
    }
    return Head{Id::of(s.text(0)), s.integer(1)};
}

void copy_header(const Header& h, Header& out) {
    header::FieldsView f;
    f.tag = h.value(Slot::tag);
    f.scope = h.value(Slot::scope);
    f.lane = h.value(Slot::lane);
    f.seq = h.seq();
    f.parent = h.text(Slot::parent);
    f.kind = h.value(Slot::kind);
    f.writer = h.value(Slot::writer);
    f.basis = h.value(Slot::basis);
    f.at = h.value(Slot::at);
    f.target = h.text(Slot::target);
    f.content = h.text(Slot::content);
    f.id = h.text(Slot::id);
    out = header::make(f, out.get_allocator());
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

Found find(Connection& c, std::string_view id, Header& out, memory::String* record, const WhereView& at) {
    Statement s = c.prepare("SELECT e.id, e.scope, e.lane, e.seq, e.parent, e.kind, e.target, e.line, b.record "
                            "FROM entry AS e LEFT JOIN body AS b ON b.id = e.id WHERE e.id = ?1",
                            at);
    s.bind_text(1, id, at);
    if (!s.step(at)) {
        return Found::absent;
    }
    if (record != nullptr && s.null(record_column)) {
        return Found::entry;
    }
    out = row_header(s, out.get_allocator(), at);
    if (record == nullptr || s.null(record_column)) {
        return Found::entry;
    }
    record->assign(s.blob(record_column));
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

bool ScanView::next(Header& out) {
    gate_->enter(Operation::scan, path_);
    if (done_) {
        return false;
    }
    const WhereView at = site(name(Operation::scan), path_);
    memory::Arena arena(pages_);
#if defined(QIVEN3_FAULTS)
    if (countdown_ != nullptr) {
        arena.attach(countdown_);
    }
#endif
    Statement s = c_->prepare("SELECT id, scope, lane, seq, parent, kind, target, line FROM entry "
                              "WHERE (scope, lane, seq) > (?1, ?2, ?3) AND (?4 IS NULL OR scope = ?4) "
                              "AND (?5 IS NULL OR lane = ?5) AND seq > ?6 AND rowid <= ?7 ORDER BY scope, lane, seq "
                              "LIMIT 1",
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
    const Header h = row_header(s, Chars(arena), at);
    copy_header(h, out);
    at_scope_ = memory::Inline<scope_bytes>(h.value(Slot::scope));
    at_lane_ = memory::Inline<lane_bytes>(h.value(Slot::lane));
    at_seq_ = h.seq();
    peak_ = std::max(peak_, arena.peak());
    return true;
}

}  // namespace qiven3::store::sqlite
