#pragma once

// codes: the engine's result codes, by number, mapped to the store's typed
// errors and to a purge's reasons. The numbers are the extended result
// codes the SQLite store reads through the C API after a failing call;
// this module holds v3's own table of their names, the C API having no call
// for one, and so includes nothing of the engine's. A code's primary code
// is its low byte, and a family is a primary code with its extended codes.
//
// to_error(code, where): the families of BUSY StoreBusy, READONLY
// StoreLocked not_writable, IOERR StoreLocked io, CANTOPEN StoreLocked held
// (StoreError missing when the store's file does not exist), CORRUPT
// StoreCorrupt corrupt, NOTADB StoreCorrupt not_a_store, FULL StoreFull
// disk_full; PRIMARYKEY (1555) and UNIQUE (2067) at an insert of append or
// forget HeadConflict, its actual head read again by the caller; any other
// constraint, and any at import, StoreError guard; anything else StoreError
// internal. detail holds the code's name and number, "SQLITE_BUSY (5)".
// NOMEM is the caller's: the heap's MemoryError, past the budget.
//
// purge_reason(code, step): a purge never throws. A step it stopped leaves
// the purge pending, its reason `locked` for the LOCKED family at any step;
// `writer` for the BUSY family at VACUUM, at raising `vacuumed` or `purged`,
// or at the probe, the BEGIN IMMEDIATE tried without waiting after a busy
// checkpoint; `reader` for the probe taken (OK, then rolled back); else the
// code's name.
//
// Allocations: none. thread: none. Every operation constant-evaluable.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "memory/memory.hpp"
#include "store/errors.hpp"

namespace qiven3::store {

// A code and its name, as the engine's header spells it, held by value.
struct Result {
    int code = 0;
    memory::Inline<32> name;
};
constexpr Result result(int code, std::string_view name) noexcept { return Result{code, memory::Inline<32>(name)}; }

// The pinned engine's 113 result codes, by number.
inline constexpr std::array<Result, 113> results = {
    result(0, "SQLITE_OK"), result(1, "SQLITE_ERROR"), result(2, "SQLITE_INTERNAL"), result(3, "SQLITE_PERM"),
    result(4, "SQLITE_ABORT"), result(5, "SQLITE_BUSY"), result(6, "SQLITE_LOCKED"), result(7, "SQLITE_NOMEM"),
    result(8, "SQLITE_READONLY"), result(9, "SQLITE_INTERRUPT"), result(10, "SQLITE_IOERR"),
    result(11, "SQLITE_CORRUPT"), result(12, "SQLITE_NOTFOUND"), result(13, "SQLITE_FULL"),
    result(14, "SQLITE_CANTOPEN"), result(15, "SQLITE_PROTOCOL"), result(16, "SQLITE_EMPTY"),
    result(17, "SQLITE_SCHEMA"), result(18, "SQLITE_TOOBIG"), result(19, "SQLITE_CONSTRAINT"),
    result(20, "SQLITE_MISMATCH"), result(21, "SQLITE_MISUSE"), result(22, "SQLITE_NOLFS"),
    result(23, "SQLITE_AUTH"), result(24, "SQLITE_FORMAT"), result(25, "SQLITE_RANGE"), result(26, "SQLITE_NOTADB"),
    result(27, "SQLITE_NOTICE"), result(28, "SQLITE_WARNING"), result(100, "SQLITE_ROW"),
    result(101, "SQLITE_DONE"), result(256, "SQLITE_OK_LOAD_PERMANENTLY"),
    result(257, "SQLITE_ERROR_MISSING_COLLSEQ"), result(261, "SQLITE_BUSY_RECOVERY"),
    result(262, "SQLITE_LOCKED_SHAREDCACHE"), result(264, "SQLITE_READONLY_RECOVERY"),
    result(266, "SQLITE_IOERR_READ"), result(267, "SQLITE_CORRUPT_VTAB"), result(270, "SQLITE_CANTOPEN_NOTEMPDIR"),
    result(275, "SQLITE_CONSTRAINT_CHECK"), result(279, "SQLITE_AUTH_USER"),
    result(283, "SQLITE_NOTICE_RECOVER_WAL"), result(284, "SQLITE_WARNING_AUTOINDEX"),
    result(512, "SQLITE_OK_SYMLINK"), result(513, "SQLITE_ERROR_RETRY"), result(516, "SQLITE_ABORT_ROLLBACK"),
    result(517, "SQLITE_BUSY_SNAPSHOT"), result(518, "SQLITE_LOCKED_VTAB"), result(520, "SQLITE_READONLY_CANTLOCK"),
    result(522, "SQLITE_IOERR_SHORT_READ"), result(523, "SQLITE_CORRUPT_SEQUENCE"),
    result(526, "SQLITE_CANTOPEN_ISDIR"), result(531, "SQLITE_CONSTRAINT_COMMITHOOK"),
    result(539, "SQLITE_NOTICE_RECOVER_ROLLBACK"), result(769, "SQLITE_ERROR_SNAPSHOT"),
    result(773, "SQLITE_BUSY_TIMEOUT"), result(776, "SQLITE_READONLY_ROLLBACK"), result(778, "SQLITE_IOERR_WRITE"),
    result(779, "SQLITE_CORRUPT_INDEX"), result(782, "SQLITE_CANTOPEN_FULLPATH"),
    result(787, "SQLITE_CONSTRAINT_FOREIGNKEY"), result(795, "SQLITE_NOTICE_RBU"),
    result(1025, "SQLITE_ERROR_RESERVESIZE"), result(1032, "SQLITE_READONLY_DBMOVED"),
    result(1034, "SQLITE_IOERR_FSYNC"), result(1038, "SQLITE_CANTOPEN_CONVPATH"),
    result(1043, "SQLITE_CONSTRAINT_FUNCTION"), result(1281, "SQLITE_ERROR_KEY"),
    result(1288, "SQLITE_READONLY_CANTINIT"), result(1290, "SQLITE_IOERR_DIR_FSYNC"),
    result(1294, "SQLITE_CANTOPEN_DIRTYWAL"), result(1299, "SQLITE_CONSTRAINT_NOTNULL"),
    result(1537, "SQLITE_ERROR_UNABLE"), result(1544, "SQLITE_READONLY_DIRECTORY"),
    result(1546, "SQLITE_IOERR_TRUNCATE"), result(1550, "SQLITE_CANTOPEN_SYMLINK"),
    result(1555, "SQLITE_CONSTRAINT_PRIMARYKEY"), result(1802, "SQLITE_IOERR_FSTAT"),
    result(1811, "SQLITE_CONSTRAINT_TRIGGER"), result(2058, "SQLITE_IOERR_UNLOCK"),
    result(2067, "SQLITE_CONSTRAINT_UNIQUE"), result(2314, "SQLITE_IOERR_RDLOCK"),
    result(2323, "SQLITE_CONSTRAINT_VTAB"), result(2570, "SQLITE_IOERR_DELETE"),
    result(2579, "SQLITE_CONSTRAINT_ROWID"), result(2826, "SQLITE_IOERR_BLOCKED"),
    result(2835, "SQLITE_CONSTRAINT_PINNED"), result(3082, "SQLITE_IOERR_NOMEM"),
    result(3091, "SQLITE_CONSTRAINT_DATATYPE"), result(3338, "SQLITE_IOERR_ACCESS"),
    result(3594, "SQLITE_IOERR_CHECKRESERVEDLOCK"), result(3850, "SQLITE_IOERR_LOCK"),
    result(4106, "SQLITE_IOERR_CLOSE"), result(4362, "SQLITE_IOERR_DIR_CLOSE"),
    result(4618, "SQLITE_IOERR_SHMOPEN"), result(4874, "SQLITE_IOERR_SHMSIZE"),
    result(5130, "SQLITE_IOERR_SHMLOCK"), result(5386, "SQLITE_IOERR_SHMMAP"), result(5642, "SQLITE_IOERR_SEEK"),
    result(5898, "SQLITE_IOERR_DELETE_NOENT"), result(6154, "SQLITE_IOERR_MMAP"),
    result(6410, "SQLITE_IOERR_GETTEMPPATH"), result(6666, "SQLITE_IOERR_CONVPATH"),
    result(6922, "SQLITE_IOERR_VNODE"), result(7178, "SQLITE_IOERR_AUTH"),
    result(7434, "SQLITE_IOERR_BEGIN_ATOMIC"), result(7690, "SQLITE_IOERR_COMMIT_ATOMIC"),
    result(7946, "SQLITE_IOERR_ROLLBACK_ATOMIC"), result(8202, "SQLITE_IOERR_DATA"),
    result(8458, "SQLITE_IOERR_CORRUPTFS"), result(8714, "SQLITE_IOERR_IN_PAGE"),
    result(8970, "SQLITE_IOERR_BADKEY"), result(9226, "SQLITE_IOERR_CODEC"),
};

// A code outside the table, as Python's error name gives it.
inline constexpr std::string_view unknown_name = "unknown";

// The primary codes the mapping reads, and the two constraints it names.
inline constexpr int busy_code = 5;
inline constexpr int locked_code = 6;
inline constexpr int readonly_code = 8;
inline constexpr int ioerr_code = 10;
inline constexpr int corrupt_code = 11;
inline constexpr int full_code = 13;
inline constexpr int cantopen_code = 14;
inline constexpr int constraint_code = 19;
inline constexpr int notadb_code = 26;
inline constexpr int primarykey_code = 1555;
inline constexpr int unique_code = 2067;

// A code's primary code, its low byte.
constexpr int primary(int code) noexcept { return static_cast<int>(static_cast<unsigned>(code) & 0xFFU); }

// A code's name by the table, unknown_name outside it: a binary search.
constexpr std::string_view result_name(int code) noexcept {
    std::size_t low = 0;
    std::size_t high = results.size();
    while (low < high) {
        const std::size_t mid = low + (high - low) / 2;
        if (results[mid].code < code) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low < results.size() && results[low].code == code ? results[low].name.view() : unknown_name;
}

// Where a statement failed, as far as the mapping reads it.
enum class Place : std::uint8_t {
    other,    // any other statement
    insert,   // an insert of append or forget
    import_,  // any statement of an import, its COMMIT under the deferral included
};

// The failing call's site: the operation and the store's path, which every
// error carries, and what one kind needs: the seconds waited for StoreBusy;
// whether the store's file exists for CANTOPEN; the scope, the lane, the
// expected head and the actual one, read again, for HeadConflict; the
// size, the count and the maxima for StoreFull.
struct WhereView {
    std::string_view operation;
    std::string_view path;
    Place place = Place::other;
    bool exists = true;
    double seconds = 0.0;
    std::string_view scope;
    std::string_view lane;
    std::string_view expected;
    std::string_view actual;
    std::uint64_t size = 0;
    std::uint64_t count = 0;
    std::uint64_t max_size = 0;
    std::uint64_t max_count = 0;
};

static_assert(sizeof(Result) == 48, "a table row's size is pinned: its code and its name in place");
static_assert(sizeof(WhereView) == 144, "a site's size is pinned: six views, its place, its seconds, four counts");

// detail's bytes: the longest name, " (", a code's digits with a sign, ")".
inline constexpr std::size_t result_detail_bytes = 48;

// "<name> (<code>)" into out; the view is out's. Each write is bounded by
// out's size, which the longest name and code fit, so that no write can
// pass it whatever a name's length.
constexpr std::string_view result_detail(int code, std::array<char, result_detail_bytes>& out) noexcept {
    std::size_t n = 0;
    const auto put = [&out, &n](char c) noexcept {
        if (n < out.size()) {
            out[n++] = c;
        }
    };
    for (const char c : result_name(code)) {
        put(c);
    }
    put(' ');
    put('(');
    if (code < 0) {
        put('-');
    }
    // The magnitude unsigned, so that the most negative code has one too.
    std::uint32_t magnitude = code < 0 ? 0U - static_cast<std::uint32_t>(code) : static_cast<std::uint32_t>(code);
    std::array<char, 10> digits{};
    std::size_t d = 0;
    do {
        digits[d++] = static_cast<char>('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && d < digits.size());
    while (d > 0) {
        put(digits[--d]);
    }
    put(')');
    return std::string_view(out.data(), n);
}

// The typed error for a failing call's code at its site.
constexpr StoreError to_error(int code, const WhereView& where) noexcept {
    std::array<char, result_detail_bytes> buffer{};
    const std::string_view detail = result_detail(code, buffer);
    const std::string_view op = where.operation;
    const std::string_view path = where.path;
    switch (primary(code)) {
    case busy_code:
        return StoreError::busy(op, path, where.seconds, detail);
    case readonly_code:
        return StoreError::locked(op, path, Cause::not_writable, detail);
    case ioerr_code:
        return StoreError::locked(op, path, Cause::io, detail);
    case cantopen_code:
        return where.exists ? StoreError::locked(op, path, Cause::held, detail)
                            : StoreError(Code::missing, op, path, detail);
    case corrupt_code:
        return StoreError::corrupt(op, path, Corrupt::corrupt, detail);
    case notadb_code:
        return StoreError::corrupt(op, path, Corrupt::not_a_store, detail);
    case full_code:
        return StoreError::full(op, path, Full::disk_full, where.size, where.count, where.max_size, where.max_count,
                                detail);
    case constraint_code:
        if (where.place == Place::insert && (code == primarykey_code || code == unique_code)) {
            return StoreError::head_conflict(op, path, where.scope, where.lane, where.expected, where.actual, detail);
        }
        return StoreError(Code::guard, op, path, detail);
    default:
        return StoreError(Code::internal, op, path, detail);
    }
}

// A purge attempt's steps (step a's read, VACUUM, raising `vacuumed`, the
// TRUNCATE checkpoint, the probe after a busy one, raising `purged`).
enum class Step : std::uint8_t { read, vacuum, vacuumed, checkpoint, probe, purged };

// The pending purge's reason for the code that stopped a step.
constexpr std::string_view purge_reason(int code, Step step) noexcept {
    if (primary(code) == locked_code) {
        return "locked";
    }
    const bool writes = step == Step::vacuum || step == Step::vacuumed || step == Step::probe || step == Step::purged;
    if (primary(code) == busy_code && writes) {
        return "writer";
    }
    if (code == 0 && step == Step::probe) {
        return "reader";
    }
    return result_name(code);
}

}  // namespace qiven3::store
