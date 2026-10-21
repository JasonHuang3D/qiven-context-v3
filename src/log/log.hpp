#pragma once

// log: levelled JSON lines into a crash ring, each an event of the
// catalogue (log/catalogue.hpp) with fields of four types alone, so that no
// run-time text can reach a line: a Name is made in constant evaluation
// from a literal, and the others are numbers. An event below the build's
// minimum, QIVEN3_LOG_MIN, compiles to nothing.
//
// A Log takes its ring's 64 KiB and its events' 4 KiB once from its
// binary's arena, and gives both back at its end. Each event builds its
// line in a fresh arena over the 4 KiB, released after the event; an event
// is noexcept, and a failed allocation drops it and counts it. Core writes
// into the ring alone: a main drains the Log into its output.
//
// thread: one (a Log and its Ops stay in the thread that made them; the
// ring's reader is the exception, log/ring.hpp).

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "log/ring.hpp"
#include "memory/arena.hpp"
#include "memory/fail.hpp"
#include "memory/memory.hpp"

#if !defined(QIVEN3_LOG_MIN)
#if defined(NDEBUG)
#define QIVEN3_LOG_MIN info
#else
#define QIVEN3_LOG_MIN trace
#endif
#endif

namespace qiven3::log {

enum class Level : std::uint8_t { trace, debug, info, warn, error };

// The build's minimum: trace in a Debug build, info in every other.
inline constexpr Level minimum = Level::QIVEN3_LOG_MIN;

// A name of at most 64 printable ASCII bytes, no '"' and no '\', made in
// constant evaluation from a literal: a run-time text does not compile.
class Name {
public:
    static constexpr std::size_t max_bytes = 64;
    // Implicit, so that a literal is a Name where one is taken.
    consteval Name(const char* literal) : text_(literal) {
        if (!valid(text_)) {
            memory::fail_fast("log::Name: empty, past 64 bytes, or not printable ASCII without a quote or a backslash");
        }
    }
    // At most 64 bytes, at least one, each printable ASCII, no '"' or '\'.
    static constexpr bool valid(std::string_view text) noexcept {
        if (text.empty() || text.size() > max_bytes) {
            return false;
        }
        for (const char c : text) {
            if (c < ' ' || c > '~' || c == '"' || c == '\\') {
                return false;
            }
        }
        return true;
    }
    [[nodiscard]] constexpr std::string_view text() const noexcept { return text_; }
    friend constexpr bool operator==(const Name& a, const Name& b) noexcept { return a.text_ == b.text_; }

private:
    std::string_view text_;
};

// The integers of a line: JSON's range, so a count past 2^53 - 1 saturates.
inline constexpr std::uint64_t max_number = 9007199254740991U;

class Count {
public:
    template <std::unsigned_integral U>
        requires(!std::same_as<U, bool>)
    constexpr explicit Count(U value) noexcept : value_(value > max_number ? max_number : value) {}
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_;
};

// Milliseconds, on the monotonic clock.
class Millis {
public:
    template <std::unsigned_integral U>
        requires(!std::same_as<U, bool>)
    constexpr explicit Millis(U value) noexcept : value_(value > max_number ? max_number : value) {}
    // The milliseconds since start, 0 if the clock reads before it.
    [[nodiscard]] static Millis since(std::chrono::steady_clock::time_point start) noexcept {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
        return Millis(ms > 0 ? static_cast<std::uint64_t>(ms) : std::uint64_t{0});
    }
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_;
};

// The catalogued name of text in table, else fallback: run-time text picks
// a name, never becomes one.
constexpr Name pick(std::string_view text, std::span<const Name> table, Name fallback) noexcept {
    for (const Name& n : table) {
        if (n.text() == text) {
            return n;
        }
    }
    return fallback;
}

constexpr Name level_name(Level level) noexcept {
    switch (level) {
    case Level::trace: return "trace";
    case Level::debug: return "debug";
    case Level::info: return "info";
    case Level::warn: return "warn";
    case Level::error: break;
    }
    return "error";
}

// A descriptor's fields' types, in its keys' order.
template <typename... F>
struct Types {};

template <typename T>
concept Field = std::same_as<T, Name> || std::same_as<T, Count> || std::same_as<T, Millis>;

namespace detail {
template <typename T>
struct AllFields : std::false_type {};
template <typename... F>
struct AllFields<Types<F...>> : std::bool_constant<(Field<F> && ...)> {
    static constexpr std::size_t size = sizeof...(F);
};
}  // namespace detail

// An event's descriptor: an empty type with its level, its name, its
// fields' keys and their types, each a Field, at most 8.
template <typename D>
concept Descriptor = std::is_empty_v<D> && requires {
    { D::level } -> std::convertible_to<Level>;
    { D::name } -> std::convertible_to<std::string_view>;
    { D::keys.size() } -> std::convertible_to<std::size_t>;
    typename D::fields;
} && detail::AllFields<typename D::fields>::value && detail::AllFields<typename D::fields>::size == D::keys.size() &&
                     D::keys.size() <= 8;

// The fields an event of D takes: its types exactly, in order.
template <typename D, typename... F>
concept FieldsOf = Descriptor<D> && std::same_as<typename D::fields, Types<F...>>;

// One field of a line, as the encoder reads it: borrowed for one event.
struct FieldView {
    std::string_view key;
    std::string_view text;  // a Name's; empty for a number
    std::uint64_t number = 0;
    bool is_text = false;
};

constexpr FieldView field(std::string_view key, const Name& n) noexcept { return {key, n.text(), 0, true}; }
constexpr FieldView field(std::string_view key, const Count& c) noexcept { return {key, {}, c.value(), false}; }
constexpr FieldView field(std::string_view key, const Millis& m) noexcept { return {key, {}, m.value(), false}; }

// A line's bytes at most, by its descriptor: the head's keys and largest
// values, then each field's, an integer at most 16 digits.
template <Descriptor D>
consteval std::size_t max_line() {
    constexpr std::size_t digits = 16;  // log, op and t, each
    std::size_t n = std::string_view(R"({"event":"","level":"error","log":,"op":,"t":})").size() +
                    std::string_view(D::name).size() + 3 * digits;
    using All = detail::AllFields<typename D::fields>;
    n += All::size;  // the comma before each field
    for (const std::string_view key : D::keys) {
        n += key.size() + 3 + Name::max_bytes + 2;  // a Name's quoted value, the larger
    }
    return n;
}

// D's name and keys as a line holds them: a Name's text each, the keys
// apart from each other and from the head's.
template <Descriptor D>
consteval bool well_formed() {
    constexpr std::array<std::string_view, 5> head{"event", "level", "log", "op", "t"};
    if (!Name::valid(D::name)) {
        return false;
    }
    for (std::size_t i = 0; i < D::keys.size(); ++i) {
        const std::string_view key = D::keys.at(i);
        if (!Name::valid(key)) {
            return false;
        }
        for (const std::string_view h : head) {
            if (key == h) {
                return false;
            }
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (D::keys.at(j) == key) {
                return false;
            }
        }
    }
    return true;
}

// A main's output for a drain: diag::Out, or a test's.
template <typename O>
concept Output = requires(O& out, std::string_view bytes) {
    { out.write(bytes) } noexcept -> std::same_as<bool>;
};

class Op;
namespace detail {
struct Emit;
}

class Log {
public:
    static constexpr std::size_t ring_bytes = 65536;
    static constexpr std::size_t event_bytes = 4096;
    static constexpr std::size_t max_line_bytes = 1024;

    // Takes the ring's span and the events' pages from arena, which outlives
    // the Log, and writes log.start. Past arena's budget, MemoryError
    // budget, nothing held.
    Log(memory::Arena& arena, Count number, Name binary);
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    Log(Log&&) = delete;
    Log& operator=(Log&&) = delete;
    ~Log();

    // The lines pushed since the last drain, written to out in at most two
    // writes, after log.dropped when lines were evicted before they were
    // drained. Allocates nothing but log.dropped's event, whose own line
    // may evict more, which the next drain counts. False when a write
    // failed, the lines counted as drained all the same.
    template <Output O>
    bool drain(O& out) noexcept {
        if (drained_ < ring_.first()) {
            const std::uint64_t lost = ring_.first() - drained_;
            drained_ = ring_.first();
            dropped(lost);
        }
        const RingView v = ring_.since(drained_);
        drained_ = ring_.next();
        bool ok = true;
        for (const std::string_view part : {v.older, v.newer}) {
            if (!part.empty() && !out.write(part)) {
                ok = false;
            }
        }
        return ok;
    }

    // The crash buffer, for diag::keep.
    [[nodiscard]] const Ring& ring() const noexcept { return ring_; }
    // The events dropped by a failed allocation.
    [[nodiscard]] std::uint64_t failed() const noexcept { return failed_; }
    // The largest bytes an event's arena took, padding included.
    [[nodiscard]] std::size_t peak() const noexcept { return peak_; }
    [[nodiscard]] std::uint64_t number() const noexcept { return number_; }

#if defined(QIVEN3_FAULTS)
    // Each event's allocations ask the countdown first; null detaches it.
    void attach(memory::Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    friend struct detail::Emit;
    friend class Op;

    void write(Level level, std::string_view event, std::uint64_t op, std::span<const FieldView> fields) noexcept;
    void dropped(std::uint64_t lost) noexcept;

    memory::Arena* parent_;
    std::span<char> ring_storage_;
    std::span<std::byte> pages_;
    Ring ring_;
    std::uint64_t number_;
    std::uint64_t ops_ = 0;
    std::uint64_t drained_ = 0;
    std::uint64_t failed_ = 0;
    std::size_t peak_ = 0;
    std::chrono::steady_clock::time_point start_;
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown_ = nullptr;
#endif
};

// An operation: numbered from 1 in its Log, op.begin at its start and
// op.end at its end; its events carry its number. A move-only value; one
// not ended ends as "dropped", peak 0. Ending one twice, or a moved-from
// one, is a broken invariant.
class Op {
public:
    Op(Op&& other) noexcept
        : log_(std::exchange(other.log_, nullptr)), id_(other.id_), name_(other.name_), start_(other.start_) {}
    Op& operator=(Op&& other) noexcept;
    Op(const Op&) = delete;
    Op& operator=(const Op&) = delete;
    ~Op();

    void end(Name outcome, Count peak) noexcept;
    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

private:
    friend struct detail::Emit;
    friend Op begin(Log& log, Name name) noexcept;
    Op(Log& log, Name name) noexcept
        : log_(&log), id_(++log.ops_), name_(name), start_(std::chrono::steady_clock::now()) {}

    Log* log_;
    std::uint64_t id_;
    Name name_;
    std::chrono::steady_clock::time_point start_;
};

namespace detail {
struct Emit {
    template <typename D, typename... F>
    static void line(Log& log, std::uint64_t op, const F&... fields) noexcept {
        static_assert(max_line<D>() <= Log::max_line_bytes, "log: an event's line past 1,024 bytes");
        const std::array<FieldView, sizeof...(F)> views = make<D>(std::index_sequence_for<F...>{}, fields...);
        log.write(D::level, D::name, op, views);
    }
    template <typename D, std::size_t... I, typename... F>
    static constexpr std::array<FieldView, sizeof...(F)> make(std::index_sequence<I...>, const F&... fields) noexcept {
        return {field(D::keys.at(I), fields)...};
    }
    static Log& of(const Op& op) noexcept {
        if (op.log_ == nullptr) {
            memory::fail_fast("log: an event of an Op ended or moved from");
        }
        return *op.log_;
    }
};
}  // namespace detail

// An event of D with its fields, outside any operation (op 0).
template <Descriptor D, typename... F>
    requires FieldsOf<D, F...>
void event([[maybe_unused]] Log& log, D /*descriptor*/, [[maybe_unused]] const F&... fields) noexcept {
    static_assert(well_formed<D>(), "log: a descriptor's name or keys");
    if constexpr (D::level >= minimum) {
        detail::Emit::line<D>(log, 0, fields...);
    }
}

// An event of D within op.
template <Descriptor D, typename... F>
    requires FieldsOf<D, F...>
void event([[maybe_unused]] const Op& op, D /*descriptor*/, [[maybe_unused]] const F&... fields) noexcept {
    static_assert(well_formed<D>(), "log: a descriptor's name or keys");
    if constexpr (D::level >= minimum) {
        detail::Emit::line<D>(detail::Emit::of(op), op.id(), fields...);
    }
}

// Starts an operation: its number, and op.begin.
Op begin(Log& log, Name name) noexcept;

#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(Name) == 16, "Name's size is pinned: one view");
static_assert(sizeof(Count) == 8 && sizeof(Millis) == 8, "Count's and Millis's sizes are pinned");
static_assert(sizeof(Op) == 40, "Op's size is pinned: the Log, the number, the name and the start");
#endif

}  // namespace qiven3::log
