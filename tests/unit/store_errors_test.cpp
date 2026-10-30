// store/errors' cases: each kind made with its fields and read back, its
// code given for its own kind alone, an EntryInvalid of code `secret` with
// no id, fields cut at a code point, the names in the text's spelling,
// evaluated by the compiler (the widths against the entry's are checked's
// cases); each kind's message line, its detail, its seconds, and a line
// cut at a code point; an error thrown by value and caught by its type;
// the fault run, over no allocation and no platform call. The runner's
// count after each case holds too.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "store/errors.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace store = qiven3::store;
using store::Cause;
using store::Code;
using store::Corrupt;
using store::Full;
using store::Kind;
using store::StoreError;

constexpr std::string_view path = "data/store.db";
constexpr std::string_view id_a = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view id_b = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

static_assert(std::is_trivially_copyable_v<StoreError>, "an error holds no heap and copies as bytes");
static_assert(std::is_nothrow_copy_constructible_v<StoreError> && std::is_nothrow_move_constructible_v<StoreError>,
              "an error is thrown by value");

// The base and each code's kind alone.
constexpr bool base_vector() {
    constexpr StoreError e(Code::whole_record, "forget", path, "entry 3");
    return e.kind() == Kind::store_error && e.code() == Code::whole_record && !e.cause() && !e.corrupt() &&
           !e.full() && e.operation() == "forget" && e.path() == path && e.detail() == "entry 3" && e.id().empty();
}
static_assert(base_vector());

constexpr bool conflict_vector() {
    constexpr StoreError e = StoreError::head_conflict("append", path, "project:qiven", "writer", id_a, "");
    return e.kind() == Kind::head_conflict && !e.code() && e.scope() == "project:qiven" && e.lane() == "writer" &&
           e.expected() == id_a && e.actual().empty() && e.detail().empty();
}
static_assert(conflict_vector());

constexpr bool invalid_vectors() {
    constexpr StoreError e = StoreError::entry_invalid("append", path, 2, id_a, "parent", "parent");
    constexpr StoreError s = StoreError::entry_invalid("append", path, 0, id_b, "secret", "content.text", "github_token");
    return e.kind() == Kind::entry_invalid && e.index() == 2 && e.id() == id_a && e.entry_code() == "parent" &&
           e.field() == "parent" && e.pattern().empty() && s.id().empty() && s.entry_code() == "secret" &&
           s.field() == "content.text" && s.pattern() == "github_token" && s.index() == 0;
}
static_assert(invalid_vectors());

// At namespace scope, off every frame: six errors pass a frame's bound.
constexpr StoreError found_v = StoreError::not_found("body", path, id_b);
constexpr StoreError busy_v = StoreError::busy("append", path, 5.0, "SQLITE_BUSY (5)");
constexpr StoreError locked_v = StoreError::locked("open", path, Cause::io, "SQLITE_IOERR_READ (266)");
constexpr StoreError corrupt_v = StoreError::corrupt("open", path, Corrupt::not_a_store);
constexpr StoreError full_v = StoreError::full("append", path, Full::store_full, 805306369, 12, 805306368, 550000);
constexpr StoreError empty_v = StoreError::not_empty("import", path, 7);

constexpr bool other_vectors() {
    return found_v.kind() == Kind::not_found && found_v.id() == id_b && busy_v.kind() == Kind::busy &&
           busy_v.seconds() == 5.0 && busy_v.detail() == "SQLITE_BUSY (5)" && locked_v.cause() == Cause::io &&
           !locked_v.corrupt() && corrupt_v.corrupt() == Corrupt::not_a_store && !corrupt_v.cause() &&
           full_v.full() == Full::store_full && full_v.size() == 805306369 && full_v.count() == 12 &&
           full_v.max_size() == 805306368 && full_v.max_count() == 550000 && empty_v.kind() == Kind::not_empty &&
           empty_v.count() == 7 && !empty_v.full();
}
static_assert(other_vectors());

// A field past its bytes is cut at a code point: of 14 bytes and a
// three-byte character, 17, an operation's 16 keep the 14; of 13 and the
// character, 16, all.
constexpr bool cut_vectors() {
    constexpr StoreError a(Code::internal, "abcdefghijklmn\xE4\xB8\xAD", path);
    constexpr StoreError b(Code::internal, "abcdefghijklm\xE4\xB8\xAD", path);
    return a.operation() == "abcdefghijklmn" && b.operation() == "abcdefghijklm\xE4\xB8\xAD";
}
static_assert(cut_vectors());

// The names in the text's order and spelling.
using Names = std::array<std::string_view, 12>;
constexpr bool same(std::span<const std::string_view> names, const Names& text, std::size_t n) {
    if (names.size() != n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (names[i] != text.at(i)) {
            return false;
        }
    }
    return true;
}
static_assert(same(store::kind_names, {"StoreError", "HeadConflict", "EntryInvalid", "NotFound", "StoreBusy",
                                       "StoreLocked", "StoreCorrupt", "StoreFull", "StoreNotEmpty"}, 9));
static_assert(same(store::code_names, {"closed", "exists", "location", "missing", "read_only", "thread", "settings",
                                       "engine_version", "guard", "internal", "forget_kind", "whole_record"}, 12));
static_assert(same(store::cause_names, {"held", "io", "not_writable", "permission"}, 4));
static_assert(same(store::corrupt_names, {"not_a_store", "corrupt", "foreign", "version", "incoherent", "id_mismatch",
                                          "body_mismatch", "forgotten_body"}, 8));
static_assert(same(store::full_names, {"store_full", "disk_full"}, 2));
static_assert(store::name(Kind::store_error) == "StoreError" && store::name(Kind::not_empty) == "StoreNotEmpty");
static_assert(store::name(Code::exists) == "exists" && store::name(Code::forget_kind) == "forget_kind" &&
              store::name(Code::whole_record) == "whole_record" && store::name(Code::engine_version) == "engine_version");
static_assert(store::name(Cause::not_writable) == "not_writable" && store::name(Cause::permission) == "permission");
static_assert(store::name(Corrupt::not_a_store) == "not_a_store" && store::name(Corrupt::forgotten_body) == "forgotten_body");
static_assert(store::name(Full::disk_full) == "disk_full");

// The widths against what fills them (the entry's, checked_reads' case).
static_assert(store::detail_bytes == qiven3::platform::detail_bytes);

// Each kind's line, made and checked away from the case's frame.
struct LineVector {
    StoreError error;
    std::string_view line;
};

const std::array<LineVector, 11>& line_vectors() {
    static const std::array<LineVector, 11> vectors{{
        {StoreError(Code::closed, "head", path), "StoreError closed: head data/store.db"},
        {StoreError(Code::forget_kind, "forget", path, "entry 1"),
         "StoreError forget_kind: forget data/store.db; entry 1"},
        {StoreError::head_conflict("append", path, "person", "loop", id_a, id_b),
         "HeadConflict: append data/store.db; scope person, lane loop, expected "
         "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa, actual "
         "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
        {StoreError::head_conflict("forget", path, "life", "writer", "", ""),
         "HeadConflict: forget data/store.db; scope life, lane writer, expected none, actual none"},
        {StoreError::entry_invalid("append", path, 0, id_a, "secret", "content.text", "aws_access_key"),
         "EntryInvalid secret: append data/store.db; entry 0, id none, field content.text, pattern "
         "aws_access_key"},
        {StoreError::not_found("header", path, id_b),
         "NotFound: header data/store.db; id "
         "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"},
        {StoreError::busy("append", path, 2.1804), "StoreBusy: append data/store.db; waited 2.180 s"},
        {StoreError::locked("open", path, Cause::held, "SQLITE_CANTOPEN (14)"),
         "StoreLocked held: open data/store.db; SQLITE_CANTOPEN (14)"},
        {StoreError::corrupt("verify", path, Corrupt::incoherent),
         "StoreCorrupt incoherent: verify data/store.db"},
        {StoreError::full("append", path, Full::disk_full, 1024, 3, 805306368, 550000, "SQLITE_FULL (13)"),
         "StoreFull disk_full: append data/store.db; size 1024, count 3, maximum 805306368 bytes, "
         "550000 entries; SQLITE_FULL (13)"},
        {StoreError::not_empty("import", path, 18446744073709551615ULL),
         "StoreNotEmpty: import data/store.db; count 18446744073709551615"},
    }};
    return vectors;
}

std::string line_of(const StoreError& error, std::size_t room) {
    std::vector<char> out(room);
    return std::string(store::message(error, out));
}

}  // namespace

QIVEN3_CASE(store_errors, lines) {
    std::size_t n = 0;
    for (const LineVector& v : line_vectors()) {
        CHECK_EQ(line_of(v.error, 2048), std::string(v.line));
        ++n;
    }
    std::cout << "store_errors.lines: " << n << " kinds' lines\n";
    CHECK_EQ(n, 11U);
}

QIVEN3_CASE(store_errors, seconds) {
    const auto waited = [](double s) {
        const std::string line = line_of(StoreError::busy("append", "p", s), 256);
        return line.substr(line.find("waited ") + 7);
    };
    CHECK_EQ(waited(0.0), std::string("0.000 s"));
    CHECK_EQ(waited(0.0004), std::string("0.000 s"));
    CHECK_EQ(waited(0.0005), std::string("0.001 s"));
    CHECK_EQ(waited(5.0), std::string("5.000 s"));
    CHECK_EQ(waited(0.05), std::string("0.050 s"));
    CHECK_EQ(waited(61.25), std::string("61.250 s"));
    CHECK_EQ(waited(-1.0), std::string("0.000 s"));
    CHECK_EQ(waited(std::numeric_limits<double>::quiet_NaN()), std::string("0.000 s"));
    CHECK_EQ(waited(std::numeric_limits<double>::infinity()), std::string("0.000 s"));
    CHECK_EQ(waited(1e300), std::string("9007199254740.991 s"));
}

// A line past its room ends at a code point: a path of a Chinese character
// cut inside it loses the whole character, and nothing follows the cut.
QIVEN3_CASE(store_errors, cut) {
    const StoreError e(Code::missing, "open", "d/\xE4\xB8\xAD\xE6\x96\x87", "detail");
    const std::string whole = line_of(e, 256);
    CHECK_EQ(whole, std::string("StoreError missing: open d/\xE4\xB8\xAD\xE6\x96\x87; detail"));
    const std::size_t head = std::string("StoreError missing: open d/").size();
    CHECK_EQ(line_of(e, head + 1), whole.substr(0, head));
    CHECK_EQ(line_of(e, head + 2), whole.substr(0, head));
    CHECK_EQ(line_of(e, head + 3), whole.substr(0, head + 3));
    CHECK_EQ(line_of(e, head + 5), whole.substr(0, head + 3));
    CHECK_EQ(line_of(e, head + 6), whole.substr(0, head + 6));
    CHECK_EQ(line_of(e, head + 7), whole.substr(0, head + 7));
    CHECK_EQ(line_of(e, 0), std::string());
    // A field's own cut, at its bytes: the path past 512 bytes.
    const std::string long_path(store::path_bytes - 1, 'a');
    const StoreError p(Code::missing, "open", long_path + "\xE4\xB8\xAD");
    CHECK_EQ(p.path(), std::string_view(long_path));
}

QIVEN3_CASE(store_errors, names) {
    // The names are distinct within each enumeration.
    const auto distinct = [](std::span<const std::string_view> names) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            for (std::size_t j = i + 1; j < names.size(); ++j) {
                if (names[i] == names[j]) {
                    return false;
                }
            }
        }
        return true;
    };
    CHECK(distinct(store::kind_names));
    CHECK(distinct(store::code_names));
    CHECK(distinct(store::cause_names));
    CHECK(distinct(store::corrupt_names));
    CHECK(distinct(store::full_names));
}

QIVEN3_CASE(store_errors, thrown) {
    bool caught = false;
    try {
        throw StoreError::locked("open", path, Cause::not_writable, "SQLITE_READONLY (8)");
    } catch (const StoreError& e) {
        caught = e.kind() == Kind::locked && e.cause() == Cause::not_writable && e.path() == path;
    }
    CHECK(caught);
}

// No allocation and no platform call: with an arena's countdown armed at
// its first allocation, every kind is made and written on its line, and
// the run completes at once, no call failed.
QIVEN3_CASE(store_errors, faults) {
#if defined(QIVEN3_FAULTS)
    std::vector<std::byte> pages(4096);
    qiven3::memory::Countdown countdown;
    std::optional<qiven3::memory::Arena> arena;
    std::size_t written = 0;
    const auto op = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        std::vector<char> out(2048);
        for (const LineVector& v : line_vectors()) {
            written += store::message(v.error, out).size();
        }
        arena->attach(nullptr);
    };
    const auto empty = [&](std::size_t) { return true; };
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<StoreError>(qiven3_ctx.log, "store_errors.message", countdown, op, empty);
    CHECK_EQ(runs.typed, 0U);
    CHECK_EQ(runs.completed_at, 1U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK(written > 0);
    CHECK_EQ(arena->used(), 0U);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS");
#endif
}
