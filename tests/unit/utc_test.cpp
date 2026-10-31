// utc's cases: the specification's vectors of `at`, of a date and of the
// clock, evaluated by the compiler and again at run time, every accepted
// `at` printed back to itself, the clock's microseconds truncated to the
// millisecond; L1's `when`, 05:00 for a message stamped 13:00 at +08:00,
// read from tests/fixtures/ (the 8-hour shift's fixture); the zone the C
// runtime reads, which tests/drive/test_utc.py sets by TZ and checks while
// these cases give the same results; printing within its budget, one
// allocation, a byte short a MemoryError budget with the caller's string
// unchanged, and that allocation failed, a typed error with nothing held.
// The runner's count after each case holds too.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if __has_include(<format>)
#include <format>
#endif

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utc.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
namespace utc = qiven3::utc;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using utc::Millis;
using utc::Refusal;
using Micros = std::chrono::sys_time<std::chrono::microseconds>;

constexpr Millis ms(std::int64_t n) { return Millis{std::chrono::milliseconds{n}}; }

// An `at` and what reading it gives: its time, or its refusal.
struct AtVector {
    std::string_view text;
    std::int64_t time;
    std::optional<Refusal> refusal;
};

constexpr std::array<AtVector, 19> at_vectors{{
    {"2026-10-17T09:30:00.000Z", 1'792'229'400'000, std::nullopt},
    {"2024-02-29T00:00:00.000Z", 1'709'164'800'000, std::nullopt},
    {"2000-02-29T12:00:00.000Z", 951'825'600'000, std::nullopt},
    {"0001-01-01T00:00:00.000Z", utc::first_ms, std::nullopt},
    {"9999-12-31T23:59:59.999Z", utc::last_ms, std::nullopt},
    {"1969-12-31T23:59:59.999Z", -1, std::nullopt},
    {"2026-02-29T00:00:00.000Z", 0, Refusal::date},
    {"1900-02-29T00:00:00.000Z", 0, Refusal::date},
    {"2026-02-30T09:30:00.000Z", 0, Refusal::date},
    {"0000-01-01T00:00:00.000Z", 0, Refusal::date},
    {"2026-10-17T24:00:00.000Z", 0, Refusal::time},
    {"2026-10-17T23:60:00.000Z", 0, Refusal::time},
    {"2026-10-17T23:59:60.000Z", 0, Refusal::time},
    {"2026-10-17T09:30:00Z", 0, Refusal::syntax},
    {"2026-10-17T09:30:00.000z", 0, Refusal::syntax},
    {"2026-10-17 09:30:00.000Z", 0, Refusal::syntax},
    {"2026-10-17T09:30:00.000Z\n", 0, Refusal::syntax},
    // The year in Arabic-Indic digits, U+0662 U+0660 U+0662 U+0666.
    {"\xd9\xa2\xd9\xa0\xd9\xa2\xd9\xa6-10-17T09:30:00.000Z", 0, Refusal::syntax},
    {"", 0, Refusal::syntax},
}};

// The clock's microseconds and the `at` they print as, truncated.
struct ClockVector {
    std::int64_t micros;
    std::string_view text;
};

constexpr std::array<ClockVector, 5> clock_vectors{{
    {0, "1970-01-01T00:00:00.000Z"},
    {1'792'229'400'000'000, "2026-10-17T09:30:00.000Z"},
    {1'792'229'400'123'999, "2026-10-17T09:30:00.123Z"},
    {951'825'600'999'999, "2000-02-29T12:00:00.999Z"},
    {253'402'300'799'999'999, "9999-12-31T23:59:59.999Z"},
}};

constexpr std::array<std::pair<std::string_view, bool>, 9> date_vectors{{
    {"2026-10-17", true},
    {"2024-02-29", true},
    {"0001-01-01", true},
    {"9999-12-31", true},
    {"2026-02-29", false},
    {"0000-01-01", false},
    {"2026-10-17T", false},
    {"2026-1-17", false},
    {"2026/10/17", false},
}};

constexpr Millis truncated(std::int64_t micros) {
    return std::chrono::floor<std::chrono::milliseconds>(Micros{std::chrono::microseconds{micros}});
}

constexpr bool printed(Millis t, std::string_view want) {
    std::string out;
    return utc::print_at(t, out) && out == want;
}

constexpr bool at_ok(const AtVector& v) {
    if (utc::check_at(v.text) != v.refusal) {
        return false;
    }
    if (v.refusal) {
        return !utc::parse_at(v.text);
    }
    return utc::parse_at(v.text) == ms(v.time) && printed(ms(v.time), v.text);
}

constexpr bool date_ok(std::string_view text, bool want) {
    return utc::valid_date(text) == want && (want || utc::check_date(text).has_value());
}

constexpr bool all_ok() {
    for (const AtVector& v : at_vectors) {
        if (!at_ok(v)) {
            return false;
        }
    }
    for (const ClockVector& v : clock_vectors) {
        if (!printed(truncated(v.micros), v.text)) {
            return false;
        }
    }
    for (const auto& [text, want] : date_vectors) {
        if (!date_ok(text, want)) {
            return false;
        }
    }
    return true;
}

static_assert(all_ok());
static_assert(utc::check_date("2026-02-30") == Refusal::date && utc::check_date("20261017") == Refusal::syntax);
static_assert(utc::printable(ms(utc::first_ms)) && !utc::printable(ms(utc::first_ms - 1)));
static_assert(utc::printable(ms(utc::last_ms)) && !utc::printable(ms(utc::last_ms + 1)));
static_assert(!utc::format_at(ms(INT64_MAX)) && !utc::format_at(ms(INT64_MIN)));
static_assert(!printed(ms(utc::last_ms + 1), "") && printed(ms(-1), "1969-12-31T23:59:59.999Z"));
// The 8-hour shift: 13:00 at +08:00 is 05:00 UTC, and 13:00 read as UTC is
// eight hours later.
static_assert(utc::parse_at("2026-10-17T05:00:00.000Z") == ms(1'792'213'200'000));
static_assert(*utc::parse_at("2026-10-17T13:00:00.000Z") - *utc::parse_at("2026-10-17T05:00:00.000Z") ==
              std::chrono::hours{8});
static_assert(sizeof(Millis) == 8 && sizeof(utc::detail::Read) == 16, "utc's sizes are pinned");

// The C runtime's offset from UTC, in seconds, at 2026-10-17T09:30:00Z: the
// zone TZ names, which utc never reads.
std::int64_t local_offset() {
    const std::time_t t = 1'792'229'400;
    std::tm tm{};
#if defined(_WIN32)
    if (localtime_s(&tm, &t) != 0) {
        return INT64_MIN;
    }
#else
    if (localtime_r(&t, &tm) == nullptr) {
        return INT64_MIN;
    }
#endif
    const std::chrono::year_month_day ymd{std::chrono::year{tm.tm_year + 1900},
                                          std::chrono::month{static_cast<unsigned>(tm.tm_mon + 1)},
                                          std::chrono::day{static_cast<unsigned>(tm.tm_mday)}};
    const std::chrono::sys_seconds local = std::chrono::sys_days{ymd} + std::chrono::hours{tm.tm_hour} +
                                           std::chrono::minutes{tm.tm_min} + std::chrono::seconds{tm.tm_sec};
    return local.time_since_epoch().count() - static_cast<std::int64_t>(t);
}

}  // namespace

QIVEN3_CASE(utc, vectors) {
    for (const AtVector& v : at_vectors) {
        CHECK(at_ok(v));
    }
    for (const ClockVector& v : clock_vectors) {
        CHECK(printed(truncated(v.micros), v.text));
#if defined(__cpp_lib_format) && defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
        // The specification's printing, by std::format, gives the same.
        CHECK_EQ(std::format("{:%FT%T}Z", truncated(v.micros)), std::string(v.text));
#endif
    }
    for (const auto& [text, want] : date_vectors) {
        CHECK(date_ok(text, want));
    }
    CHECK(all_ok());
#if defined(__cpp_lib_format) && defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    constexpr std::string_view by_format = "compared with std::format";
#else
    constexpr std::string_view by_format = "std::format not compared: the library lacks it";
#endif
    std::cout << "utc.vectors: " << at_vectors.size() << " at, " << clock_vectors.size() << " clock, "
              << date_vectors.size() << " date; " << by_format << "\n";
}

// L1's `when` holds 05:00 for a message stamped 13:00 at +08:00.
QIVEN3_CASE(utc, shift) {
    canon::Basic<> entries;
    fixtures::load_entries(entries);
    canon::Basic<> content;
    canon::parse(*fixtures::find_entry(entries, "L1")->content, content);
    const std::optional<qiven3::json::Index> when = content.find(0, "when");
    CHECK(when.has_value());
    const std::optional<qiven3::json::Index> time = content.find(*when, "time");
    CHECK(time.has_value());
    const std::string_view text = content.text(*time);
    CHECK_EQ(std::string(text), std::string("2026-10-17T05:00:00.000Z"));
    CHECK(utc::parse_at(text) == ms(1'792'213'200'000));
    CHECK(*utc::parse_at("2026-10-17T13:00:00.000Z") - *utc::parse_at(text) == std::chrono::hours{8});
    std::string out;
    CHECK(utc::print_at(*utc::parse_at(text), out) && out == text);
}

// The zone the process's C runtime reads; tests/drive/test_utc.py reads
// the line under each TZ it sets.
QIVEN3_CASE(utc, zone) {
    const std::int64_t offset = local_offset();
    CHECK(offset != INT64_MIN);
    std::cout << "utc.zone: local offset " << offset << " s\n";
    CHECK(utc::parse_at("2026-10-17T09:30:00.000Z") == ms(1'792'229'400'000));
    CHECK(printed(ms(1'792'229'400'000), "2026-10-17T09:30:00.000Z"));
}

// print_at's one allocation, its 24 characters, within 64 bytes; reading
// takes none.
QIVEN3_CASE(utc, budgets) {
    constexpr std::size_t budget = 64;
    Arena& arena = qiven3_ctx.counted.arena(budget);
    std::size_t peak = 0;
    {
        String out{Allocator<char>(arena)};
        CHECK(utc::print_at(ms(1'792'229'400'123), out));
        CHECK_EQ(std::string(out), std::string("2026-10-17T09:30:00.123Z"));
        peak = arena.peak();
        String refused{Allocator<char>(arena)};
        CHECK(!utc::print_at(ms(utc::last_ms + 1), refused));
        CHECK(refused.empty());
    }
    std::cout << "utc.print_at: peak " << peak << ", budget " << budget << "\n";
    CHECK(peak > 0 && peak <= budget);
    CHECK(arena.used() == 0);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
    String out{Allocator<char>(shorter)};
    bool refused = false;
    try {
        static_cast<void>(utc::print_at(ms(0), out));
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget;
    }
    CHECK(refused);
    CHECK(out.empty());
#else
    std::cout << "utc.print_at: a byte short not run, under MSVC's debug containers\n";
#endif
}

QIVEN3_CASE(utc, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(64);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const qiven3::test::FaultRuns r = qiven3::test::run_faults<MemoryError>(
        qiven3_ctx.log, "utc.print_at", countdown,
        [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            String out{Allocator<char>(*arena)};
            static_cast<void>(utc::print_at(ms(1'792'229'400'000), out));
        },
        [&](std::size_t) { return arena->used() == 0; });
    arena.reset();
    // The result's one allocation.
    CHECK_EQ(r.typed, 1U);
    CHECK_EQ(r.completed_at, 2U);
    CHECK_EQ(r.untyped, 0U);
    CHECK_EQ(r.unchecked, 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
