#pragma once

// utc: an entry's `at` and a date, read and printed. An `at` is
// YYYY-MM-DDTHH:MM:SS.mmmZ, 24 ASCII characters: a real date by
// std::chrono's year_month_day, its year from 1, since std::chrono has a
// year 0 and the text none; hours to 23, minutes and seconds to 59, no leap
// second. A date is its first 10 characters alone. No other form is read,
// nor a time zone: the reading and the printing are arithmetic on the
// text and the count, and never consult the process's zone. A time is UTC
// as milliseconds since 1970, the platform's now() type; printing writes
// the 24 characters of a time from year 1 to year 9999, the count floored
// to the day first. Every function is pure and constexpr, the printing a
// template on the string it writes into, so its vectors run in constant
// evaluation too; it reads by index, with no pointer arithmetic. Reading
// and the fixed-size printing take no allocation; print_at one, its
// result's. utc logs nothing.
//
// thread: none

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace qiven3::utc {

// UTC, milliseconds since 1970: the platform's now().
using Millis = std::chrono::sys_time<std::chrono::milliseconds>;

inline constexpr std::size_t at_size = 24;
inline constexpr std::size_t date_size = 10;

// The range printing covers: 0001-01-01T00:00:00.000Z to
// 9999-12-31T23:59:59.999Z, in milliseconds since 1970.
inline constexpr std::int64_t first_ms = -62'135'596'800'000;
inline constexpr std::int64_t last_ms = 253'402'300'799'999;

// Why a text is not an `at` or a date: its characters, a date that does not
// exist, or a time that does not.
enum class Refusal : std::uint8_t { syntax, date, time };

namespace detail {

constexpr bool digit(char c) noexcept { return c >= '0' && c <= '9'; }

// The ASCII digits text[at, at + n) as a number; the caller checked them.
constexpr int number(std::string_view text, std::size_t at, std::size_t n) noexcept {
    int value = 0;
    for (std::size_t i = at; i < at + n; ++i) {
        value = value * 10 + (text[i] - '0');
    }
    return value;
}

// Whether text matches the pattern, where '9' is an ASCII digit and any
// other character itself.
constexpr bool shaped(std::string_view text, std::string_view pattern) noexcept {
    if (text.size() != pattern.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (pattern[i] == '9' ? !digit(text[i]) : text[i] != pattern[i]) {
            return false;
        }
    }
    return true;
}

inline constexpr std::string_view at_shape = "9999-99-99T99:99:99.999Z";

// The day text[0, 10) names, its shape checked: none where no such date is.
constexpr std::optional<std::chrono::sys_days> day(std::string_view text) noexcept {
    const int y = number(text, 0, 4);
    const auto m = static_cast<unsigned>(number(text, 5, 2));
    const auto d = static_cast<unsigned>(number(text, 8, 2));
    const std::chrono::year_month_day ymd{std::chrono::year{y}, std::chrono::month{m}, std::chrono::day{d}};
    if (y < 1 || !ymd.ok()) {
        return std::nullopt;
    }
    return std::chrono::sys_days{ymd};
}

// The text read as an `at`: its time, or why not.
struct Read {
    Millis time{};
    std::optional<Refusal> refusal;
};

constexpr Read read_at(std::string_view text) noexcept {
    if (!shaped(text, at_shape)) {
        return Read{Millis{}, Refusal::syntax};
    }
    const std::optional<std::chrono::sys_days> d = day(text);
    if (!d) {
        return Read{Millis{}, Refusal::date};
    }
    const int h = number(text, 11, 2);
    const int min = number(text, 14, 2);
    const int s = number(text, 17, 2);
    if (h > 23 || min > 59 || s > 59) {
        return Read{Millis{}, Refusal::time};
    }
    const Millis t = Millis{*d} + std::chrono::hours{h} + std::chrono::minutes{min} + std::chrono::seconds{s} +
                     std::chrono::milliseconds{number(text, 20, 3)};
    return Read{t, std::nullopt};
}

constexpr void put(std::array<char, at_size>& out, std::size_t at, std::size_t n, std::int64_t value) noexcept {
    for (std::size_t i = at + n; i > at; --i) {
        out[i - 1] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
}

}  // namespace detail

// Why text is not an `at`, or none where it is one.
constexpr std::optional<Refusal> check_at(std::string_view text) noexcept { return detail::read_at(text).refusal; }

// The time an `at` names, or none for a text that is not one.
constexpr std::optional<Millis> parse_at(std::string_view text) noexcept {
    const detail::Read r = detail::read_at(text);
    if (r.refusal) {
        return std::nullopt;
    }
    return r.time;
}

// Why text is not a date, YYYY-MM-DD, or none where it is one.
constexpr std::optional<Refusal> check_date(std::string_view text) noexcept {
    if (!detail::shaped(text, detail::at_shape.substr(0, date_size))) {
        return Refusal::syntax;
    }
    return detail::day(text) ? std::nullopt : std::optional<Refusal>(Refusal::date);
}

constexpr bool valid_date(std::string_view text) noexcept { return !check_date(text); }

// Whether t is within the years printing covers.
constexpr bool printable(Millis t) noexcept {
    const std::int64_t ms = t.time_since_epoch().count();
    return ms >= first_ms && ms <= last_ms;
}

// t's 24 characters, no allocation; none for a time printable() refuses.
constexpr std::optional<std::array<char, at_size>> format_at(Millis t) noexcept {
    if (!printable(t)) {
        return std::nullopt;
    }
    const std::chrono::sys_days d = std::chrono::floor<std::chrono::days>(t);
    const std::chrono::year_month_day ymd{d};
    const std::int64_t of_day = (t - Millis{d}).count();
    std::array<char, at_size> out{};
    for (std::size_t i = 0; i < at_size; ++i) {
        out[i] = detail::at_shape[i];
    }
    detail::put(out, 0, 4, static_cast<int>(ymd.year()));
    detail::put(out, 5, 2, static_cast<unsigned>(ymd.month()));
    detail::put(out, 8, 2, static_cast<unsigned>(ymd.day()));
    detail::put(out, 11, 2, of_day / 3'600'000);
    detail::put(out, 14, 2, of_day / 60'000 % 60);
    detail::put(out, 17, 2, of_day / 1'000 % 60);
    detail::put(out, 20, 3, of_day % 1'000);
    return out;
}

// t's 24 characters into out, a string type, replaced on success: one
// allocation, the result's; false, out unchanged, for a time printable()
// refuses. A MemoryError leaves out unchanged too.
template <typename S>
[[nodiscard]] constexpr bool print_at(Millis t, S& out) {
    const std::optional<std::array<char, at_size>> chars = format_at(t);
    if (!chars) {
        return false;
    }
    S text(out.get_allocator());
    text.reserve(at_size);
    text.append(std::string_view(chars->data(), chars->size()));
    out = std::move(text);
    return true;
}

}  // namespace qiven3::utc
