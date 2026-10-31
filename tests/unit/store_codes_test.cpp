// store/codes' cases: the table's thirteen named numbers, its order, its
// names whole and unknown outside it; each row of the text's mapping by
// number and site, HeadConflict and guard by place, StoreBusy's seconds,
// CANTOPEN's two errors, StoreFull's counts; the purge's reasons by code
// and step; each evaluated by the compiler. Then every code of the table,
// and codes outside it, at every place and every step against the families
// read apart; an error's line with its detail; the fault run, over no
// allocation and no platform call. The runner's count after each case
// holds too.

#include <array>
#include <climits>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "store/codes.hpp"
#include "store/errors.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace store = qiven3::store;
using store::Cause;
using store::Code;
using store::Corrupt;
using store::Full;
using store::Kind;
using store::Place;
using store::Step;
using store::StoreError;
using store::WhereView;

constexpr std::string_view path = "data/store.db";
constexpr std::string_view id_a = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view id_b = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

// The text's thirteen codes by number, each with its name.
constexpr bool named_vector() {
    return store::result_name(5) == "SQLITE_BUSY" && store::result_name(517) == "SQLITE_BUSY_SNAPSHOT" &&
           store::result_name(6) == "SQLITE_LOCKED" && store::result_name(8) == "SQLITE_READONLY" &&
           store::result_name(10) == "SQLITE_IOERR" && store::result_name(266) == "SQLITE_IOERR_READ" &&
           store::result_name(11) == "SQLITE_CORRUPT" && store::result_name(13) == "SQLITE_FULL" &&
           store::result_name(14) == "SQLITE_CANTOPEN" && store::result_name(26) == "SQLITE_NOTADB" &&
           store::result_name(1555) == "SQLITE_CONSTRAINT_PRIMARYKEY" &&
           store::result_name(2067) == "SQLITE_CONSTRAINT_UNIQUE" &&
           store::result_name(787) == "SQLITE_CONSTRAINT_FOREIGNKEY";
}
static_assert(named_vector());

// The table: by number, strictly rising; each name whole (none cut by its
// width), spelled as the header's; the ends found; none between.
constexpr bool table_vector() {
    for (std::size_t i = 0; i < store::results.size(); ++i) {
        const std::string_view name = store::results[i].name.view();
        if ((i > 0 && store::results[i - 1].code >= store::results[i].code) || name.size() >= 32 ||
            name.substr(0, 7) != "SQLITE_" || store::result_name(store::results[i].code) != name) {
            return false;
        }
    }
    return store::result_name(0) == "SQLITE_OK" && store::result_name(9226) == "SQLITE_IOERR_CODEC" &&
           store::result_name(-1) == "unknown" && store::result_name(29) == "unknown" &&
           store::result_name(102) == "unknown" && store::result_name(9227) == "unknown" &&
           store::result_name(INT_MIN) == "unknown" && store::result_name(INT_MAX) == "unknown";
}
static_assert(table_vector());

// detail: the name and the number; the widest fits.
constexpr bool detail_vector() {
    std::array<char, store::result_detail_bytes> out{};
    const bool read = store::result_detail(266, out) == "SQLITE_IOERR_READ (266)";
    const bool zero = store::result_detail(0, out) == "SQLITE_OK (0)";
    const bool low = store::result_detail(INT_MIN, out) == "unknown (-2147483648)";
    const bool wide = store::result_detail(3594, out) == "SQLITE_IOERR_CHECKRESERVEDLOCK (3594)";
    return read && zero && low && wide;
}
static_assert(detail_vector());
static_assert(std::string_view("SQLITE_IOERR_CHECKRESERVEDLOCK (-2147483648)").size() <= store::result_detail_bytes);

constexpr WhereView at(std::string_view operation, Place place = Place::other) {
    WhereView w;
    w.operation = operation;
    w.path = path;
    w.place = place;
    return w;
}

// Each helper makes one error in its own frame (an error is 1,392 bytes)
// and checks its kind, its fields and its detail.
constexpr bool busy(int code, std::string_view detail) {
    WhereView w = at("append");
    w.seconds = 5.0;
    const StoreError e = store::to_error(code, w);
    return e.kind() == Kind::busy && e.seconds() == 5.0 && e.operation() == "append" && e.path() == path &&
           e.detail() == detail;
}
constexpr bool locked(int code, Cause cause, std::string_view detail) {
    const StoreError e = store::to_error(code, at("open"));
    return e.kind() == Kind::locked && e.cause() == cause && e.operation() == "open" && e.detail() == detail;
}
constexpr bool missing() {
    WhereView w = at("open");
    w.exists = false;
    const StoreError e = store::to_error(14, w);
    return e.code() == Code::missing && e.path() == path && e.detail() == "SQLITE_CANTOPEN (14)";
}
constexpr bool corrupt(int code, Corrupt kind) {
    const StoreError e = store::to_error(code, at("head"));
    return e.kind() == Kind::corrupt && e.corrupt() == kind;
}
constexpr bool full() {
    WhereView w = at("append");
    w.size = 100;
    w.count = 3;
    w.max_size = 200;
    w.max_count = 10;
    const StoreError e = store::to_error(13, w);
    return e.full() == Full::disk_full && e.size() == 100 && e.count() == 3 && e.max_size() == 200 &&
           e.max_count() == 10 && e.detail() == "SQLITE_FULL (13)";
}
constexpr bool conflict(int code, std::string_view detail) {
    WhereView w = at("forget", Place::insert);
    w.scope = "alpha";
    w.lane = "main";
    w.expected = id_a;
    w.actual = id_b;
    const StoreError e = store::to_error(code, w);
    return e.kind() == Kind::head_conflict && e.scope() == "alpha" && e.lane() == "main" && e.expected() == id_a &&
           e.actual() == id_b && e.operation() == "forget" && e.detail() == detail;
}
constexpr bool own(int code, Place place, Code expected, std::string_view detail) {
    const StoreError e = store::to_error(code, at("append", place));
    return e.kind() == Kind::store_error && e.code() == expected && e.detail() == detail;
}

// The text's rows: BUSY and its extended codes StoreBusy with the seconds
// waited; READONLY not_writable; IOERR and its extended codes io.
static_assert(busy(5, "SQLITE_BUSY (5)") && busy(517, "SQLITE_BUSY_SNAPSHOT (517)"));
static_assert(locked(8, Cause::not_writable, "SQLITE_READONLY (8)") &&
              locked(266, Cause::io, "SQLITE_IOERR_READ (266)") && locked(10, Cause::io, "SQLITE_IOERR (10)"));

// CANTOPEN: StoreLocked held, StoreError missing when the file does not
// exist; CORRUPT, NOTADB and FULL.
static_assert(locked(14, Cause::held, "SQLITE_CANTOPEN (14)") && missing());
static_assert(corrupt(11, Corrupt::corrupt) && corrupt(779, Corrupt::corrupt) && corrupt(26, Corrupt::not_a_store));
static_assert(full());

// 1555 and 2067 at an insert of append or forget HeadConflict with the
// head read again; at import, at another statement, or another constraint,
// StoreError guard; anything else internal, an unknown code by its number.
static_assert(conflict(1555, "SQLITE_CONSTRAINT_PRIMARYKEY (1555)") &&
              conflict(2067, "SQLITE_CONSTRAINT_UNIQUE (2067)"));
static_assert(own(787, Place::insert, Code::guard, "SQLITE_CONSTRAINT_FOREIGNKEY (787)") &&
              own(19, Place::insert, Code::guard, "SQLITE_CONSTRAINT (19)") &&
              own(1555, Place::import_, Code::guard, "SQLITE_CONSTRAINT_PRIMARYKEY (1555)") &&
              own(2067, Place::other, Code::guard, "SQLITE_CONSTRAINT_UNIQUE (2067)"));
static_assert(own(1, Place::other, Code::internal, "SQLITE_ERROR (1)") &&
              own(7, Place::other, Code::internal, "SQLITE_NOMEM (7)") &&
              own(6, Place::other, Code::internal, "SQLITE_LOCKED (6)") &&
              own(99999, Place::other, Code::internal, "unknown (99999)"));

// A purge's reasons: writer for BUSY at VACUUM, at either raise or at the
// probe; reader for the probe taken; locked for LOCKED at any step; else
// the code's name, BUSY at the read or the checkpoint among them.
constexpr bool purge_vector() {
    return store::purge_reason(5, Step::vacuum) == "writer" && store::purge_reason(5, Step::vacuumed) == "writer" &&
           store::purge_reason(5, Step::probe) == "writer" && store::purge_reason(5, Step::purged) == "writer" &&
           store::purge_reason(517, Step::vacuum) == "writer" && store::purge_reason(0, Step::probe) == "reader" &&
           store::purge_reason(6, Step::checkpoint) == "locked" && store::purge_reason(262, Step::vacuum) == "locked" &&
           store::purge_reason(5, Step::checkpoint) == "SQLITE_BUSY" &&
           store::purge_reason(5, Step::read) == "SQLITE_BUSY" && store::purge_reason(1, Step::vacuum) == "SQLITE_ERROR" &&
           store::purge_reason(10, Step::checkpoint) == "SQLITE_IOERR" &&
           store::purge_reason(0, Step::checkpoint) == "SQLITE_OK" && store::purge_reason(29, Step::probe) == "unknown";
}
static_assert(purge_vector());

constexpr std::array<Place, 3> places = {Place::other, Place::insert, Place::import_};
constexpr std::array<Step, 6> steps = {Step::read,       Step::vacuum, Step::vacuumed,
                                       Step::checkpoint, Step::probe,  Step::purged};

// The kind each family gives, read apart from the mapping.
struct Expected {
    Kind kind = Kind::store_error;
    std::optional<Code> code;
};
Expected expected(int code, Place place) {
    switch (code % 256 < 0 ? code % 256 + 256 : code % 256) {
    case 5:
        return {Kind::busy, std::nullopt};
    case 8:
    case 10:
    case 14:
        return {Kind::locked, std::nullopt};
    case 11:
    case 26:
        return {Kind::corrupt, std::nullopt};
    case 13:
        return {Kind::full, std::nullopt};
    case 19:
        if (place == Place::insert && (code == 1555 || code == 2067)) {
            return {Kind::head_conflict, std::nullopt};
        }
        return {Kind::store_error, Code::guard};
    default:
        return {Kind::store_error, Code::internal};
    }
}

// Codes outside the table: neighbours of its rows, and the ends of int.
std::vector<int> outside() {
    std::vector<int> codes = {-1, INT_MIN, INT_MAX, 29, 99, 102, 9227, 65536 + 5, 65536 + 19};
    return codes;
}

}  // namespace

// Every code of the table and outside it, at every place: the family's
// kind, the detail's name and number, the site's operation and path; at
// every step, a reason of the four kinds the text names.
QIVEN_CASE(store_codes, sweep) {
    std::size_t n = 0;
    std::vector<int> codes;
    for (const store::Result& r : store::results) {
        codes.push_back(r.code);
    }
    for (const int c : outside()) {
        codes.push_back(c);
    }
    for (const int c : codes) {
        std::array<char, store::result_detail_bytes> buffer{};
        const std::string_view detail = store::result_detail(c, buffer);
        for (const Place p : places) {
            const StoreError e = store::to_error(c, at("append", p));
            const Expected x = expected(c, p);
            CHECK(e.kind() == x.kind);
            CHECK(e.code() == x.code);
            CHECK_EQ(e.detail(), detail);
            CHECK_EQ(e.operation(), std::string_view("append"));
            CHECK_EQ(e.path(), path);
            ++n;
        }
        for (const Step s : steps) {
            const std::string_view reason = store::purge_reason(c, s);
            CHECK(reason == "writer" || reason == "reader" || reason == "locked" || reason == store::result_name(c));
            ++n;
        }
    }
    CHECK_EQ(n, (store::results.size() + outside().size()) * (places.size() + steps.size()));
}

// An error from a code, on its line: its kind, its cause and the detail.
QIVEN_CASE(store_codes, line) {
    const StoreError e = store::to_error(266, at("open"));
    std::vector<char> out(2048);
    const std::string_view line = store::message(e, out);
    CHECK(line.find("StoreLocked") != std::string_view::npos);
    CHECK(line.find("io") != std::string_view::npos);
    CHECK(line.find("SQLITE_IOERR_READ (266)") != std::string_view::npos);
}

// No allocation and no platform call: with an arena's countdown armed at
// its first allocation, every code is mapped at every place and step, and
// the run completes at once, no call failed.
QIVEN_CASE(store_codes, faults) {
#if defined(QIVEN_FAULTS)
    std::vector<std::byte> pages(4096);
    qiven3::memory::Countdown countdown;
    std::optional<qiven3::memory::Arena> arena;
    std::size_t mapped = 0;
    const auto op = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        for (const store::Result& r : store::results) {
            for (const Place p : places) {
                mapped += store::to_error(r.code, at("append", p)).detail().size();
            }
            for (const Step s : steps) {
                mapped += store::purge_reason(r.code, s).size();
            }
        }
        arena->attach(nullptr);
    };
    const auto empty = [&](std::size_t) { return true; };
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<StoreError>(qiven_ctx.log, "store_codes.map", countdown, op, empty);
    CHECK_EQ(runs.typed, 0U);
    CHECK_EQ(runs.completed_at, 1U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK(mapped > 0);
    CHECK_EQ(arena->used(), 0U);
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
