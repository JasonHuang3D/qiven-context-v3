#pragma once

// fold: the order of a scope's entries, a record's fold and "active".
// order sorts entries by (at, lane, seq): at as text, chronological for its
// fixed syntax; lane by code point; seq as a number. fold computes a
// record's served state when read, never stored, in the format's steps,
// the first that ends it deciding:
// 1. versions: the root, then every revision whose target is a version;
// 2. withdrawn: each version a retire targets, by its header alone;
// 3. leaves: the versions not withdrawn that no version not withdrawn
//    targets; none, and the record is retired;
// 4. current: the last leaf in the order, the others counted as forks;
// 5. current forgotten (its body not held, or a forget targets it): not
//    served, and no earlier version instead;
// 6. current's expires at or before now: expired;
// 7. a live check of current (its body held, no retire of it) in the
//    refuted row ends it: result refuted by another writer, method test or
//    rerun, or transcript re-verified;
// 8. served.
// A record is active while steps 1 to 5 give it a current version: an
// expired, forked or refuted record stays active.
//
// A record is the caller's entries in the order: its root first, then the
// entries that act on it, each after its target. An entry whose target is
// not among the earlier ones, another root, or a revision of another kind
// takes no part. The record's own faults are FoldError: an entry not after
// the one before it (order), a first entry that is no root (root). now is
// a time in at's syntax, else FoldError now. Two writers are one when equal
// or of one session n (<role>/s<n>); loop and import are each only itself.
// The caller gives, from each entry's content, expires, a check's method
// and result, and whether a transcript check re-verified; item reads them
// from a parsed content.
//
// Allocations: fold one, its work of 8 bytes an entry, freed before it
// returns; a record past 2^32 - 2 entries is MemoryError budget before it.
// order, item, one_writer and the accessors none. Each is constexpr and a
// template on the allocator, so its vectors run in constant evaluation
// too; it reads by index, with no pointer arithmetic. fold logs nothing.
//
// thread: none

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utc.hpp"

namespace qiven3::entry::fold {

// A check's method and result, read from its content; none elsewhere.
enum class Method : std::uint8_t { none, test, rerun, transcript, review };
enum class Result : std::uint8_t { none, verified, refuted };
// A transcript check's digest against the reader's bytes: no bytes
// (unverified, v3's case), equal (reverified), or unequal, when the entry
// counts for nothing.
enum class Transcript : std::uint8_t { unverified, reverified, unequal };

inline constexpr std::array<std::string_view, 4> method_names = {"test", "rerun", "transcript", "review"};
inline constexpr std::array<std::string_view, 2> result_names = {"verified", "refuted"};

// An entry as the fold reads it: views of its header's and content's text,
// made and dropped within the call that lent them. target and expires are
// empty when absent; held is false once the body is forgotten.
struct ItemView {
    std::string_view id;
    std::string_view kind;
    std::string_view target;
    std::string_view at;
    std::string_view lane;
    std::string_view writer;
    std::string_view expires;
    std::int64_t seq = 0;
    Method method = Method::none;
    Result result = Result::none;
    Transcript transcript = Transcript::unverified;
    bool held = true;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(ItemView) == 128, "ItemView's size is pinned: seven views, seq and four flags, padded");
#endif

// The fold's own faults, each with the entry's index in the record.
enum class Code : std::uint8_t { order, root, now };
inline constexpr std::array<std::string_view, 3> code_names = {"order", "root", "now"};
constexpr std::string_view name(Code code) noexcept { return code_names[static_cast<std::size_t>(code)]; }
struct FoldError {
    Code code;
    std::uint32_t index;
};
static_assert(sizeof(FoldError) == 8, "FoldError's size is pinned: a code and an index");

// The step that ended the fold: served (step 8), or not served by retired
// (3), forgotten (5), expired (6) or refuted (7).
enum class Status : std::uint8_t { served, retired, forgotten, expired, refuted };
inline constexpr std::array<std::string_view, 5> status_names = {"served", "retired", "forgotten", "expired",
                                                                 "refuted"};
inline constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t max_entries = none - 1U;

// A record's state: indices into the record, none where a step gave none.
struct State {
    Status status = Status::retired;
    std::uint32_t current = none;
    std::uint32_t forks = 0;
    std::uint32_t refuter = none;

    // Steps 1 to 5 gave a current version, neither retired nor forgotten.
    [[nodiscard]] constexpr bool active() const noexcept {
        return status != Status::retired && status != Status::forgotten;
    }
    [[nodiscard]] constexpr bool served() const noexcept { return status == Status::served; }
    constexpr bool operator==(const State&) const = default;
};
static_assert(sizeof(State) == 16, "State's size is pinned: its status, padded, and three indices");

namespace detail {

// (at, lane, seq): at and lane by their bytes, which for at's fixed syntax
// is time and for UTF-8 is code point order; seq as a number.
constexpr bool before(const ItemView& a, const ItemView& b) noexcept {
    if (a.at != b.at) {
        return a.at < b.at;
    }
    if (a.lane != b.lane) {
        return a.lane < b.lane;
    }
    return a.seq < b.seq;
}

// A writer's session n, as its digits after "/s"; none for loop and import.
constexpr std::string_view session(std::string_view writer) noexcept {
    const std::size_t slash = writer.find('/');
    if (slash == std::string_view::npos || slash + 2 >= writer.size() || writer[slash + 1] != 's') {
        return {};
    }
    return writer.substr(slash + 2);
}

template <std::size_t N>
constexpr std::size_t position(const std::array<std::string_view, N>& names, std::string_view value) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == value) {
            return i + 1;
        }
    }
    return 0;
}

constexpr bool change(std::string_view kind) noexcept {
    return position(kinds::CHANGE_KINDS, kind) != 0;
}

// The fold's work for one entry: its target's index, and what the record
// says of it.
namespace mark {
inline constexpr std::uint8_t version = 1U;
inline constexpr std::uint8_t retired = 2U;     // a retire targets it
inline constexpr std::uint8_t forgotten = 4U;   // a forget targets it
inline constexpr std::uint8_t superseded = 8U;  // a version not withdrawn targets it
}  // namespace mark
struct Work {
    std::uint32_t target = none;
    std::uint8_t marks = 0;
};
static_assert(sizeof(Work) == 8, "Work's size is pinned: an index and the marks, padded");
constexpr void add(Work& w, std::uint8_t m) noexcept { w.marks = static_cast<std::uint8_t>(w.marks | m); }
constexpr bool has(const Work& w, std::uint8_t m) noexcept { return (w.marks & m) != 0; }

[[noreturn]] inline void too_large(std::size_t entries) {
    throw memory::MemoryError::budget("fold", max_entries * sizeof(Work),
                                      memory::mul(entries, sizeof(Work)).value_or(SIZE_MAX), 0);
}

}  // namespace detail

// Two writers are one when equal or of one session n.
constexpr bool one_writer(std::string_view a, std::string_view b) noexcept {
    if (a == b) {
        return true;
    }
    const std::string_view n = detail::session(a);
    return !n.empty() && n == detail::session(b);
}

// entries sorted into the fold's order, in place.
constexpr void order(std::span<ItemView> entries) noexcept { std::sort(entries.begin(), entries.end(), detail::before); }

// A header's and content's entry as the fold reads it, with views of
// both; content empty when the entry has none or its body is not held.
template <typename A, typename B>
constexpr ItemView item(const header::Header<A>& h, const canon::Basic<B>& content, bool held) {
    using header::Slot;
    ItemView e;
    e.id = h.value(Slot::id);
    e.kind = h.value(Slot::kind);
    e.target = h.value(Slot::target);
    e.at = h.value(Slot::at);
    e.lane = h.value(Slot::lane);
    e.writer = h.value(Slot::writer);
    e.seq = h.seq();
    e.held = held;
    e.expires = kinds::detail::member(content, 0, "expires");
    if (e.kind == "check") {
        e.method = static_cast<Method>(detail::position(method_names, kinds::detail::member(content, 0, "method")));
        e.result = static_cast<Result>(detail::position(result_names, kinds::detail::member(content, 0, "result")));
    }
    return e;
}

// The record's state at now, a time in at's syntax.
template <typename A = std::allocator<char>>
constexpr State fold(std::span<const ItemView> record, std::string_view now, const A& allocator = A()) {
    namespace mark = detail::mark;
    if (utc::check_at(now)) {
        throw FoldError{Code::now, 0};
    }
    const std::size_t n = record.size();
    if (n == 0 || detail::change(record[0].kind) || !record[0].target.empty()) {
        throw FoldError{Code::root, 0};
    }
    for (std::size_t i = 1; i < n; ++i) {
        if (!detail::before(record[i - 1], record[i])) {
            throw FoldError{Code::order, static_cast<std::uint32_t>(std::min(i, max_entries))};
        }
    }
    if (n > max_entries || !memory::mul(n, sizeof(detail::Work))) {
        detail::too_large(n);
    }
    using Rebound = typename std::allocator_traits<A>::template rebind_alloc<detail::Work>;
    std::vector<detail::Work, Rebound> work(n, detail::Work{}, Rebound(allocator));
    // Steps 1 and 2, with the forgets: each entry's target among the
    // earlier ones, the versions, and what each change marks.
    detail::add(work[0], mark::version);
    for (std::size_t i = 1; i < n; ++i) {
        const ItemView& e = record[i];
        std::size_t t = 0;
        while (t < i && record[t].id != e.target) {
            ++t;
        }
        if (e.target.empty() || t == i) {
            continue;
        }
        work[i].target = static_cast<std::uint32_t>(t);
        if (e.kind == "retire") {
            detail::add(work[t], mark::retired);
        } else if (e.kind == "forget") {
            detail::add(work[t], mark::forgotten);
        } else if (!detail::change(e.kind) && e.kind == record[0].kind && detail::has(work[t], mark::version)) {
            detail::add(work[i], mark::version);
        }
    }
    const auto live_version = [&](std::size_t i) {
        return detail::has(work[i], mark::version) && !detail::has(work[i], mark::retired);
    };
    // Step 3: each live version's target is superseded.
    for (std::size_t i = 1; i < n; ++i) {
        if (live_version(i)) {
            detail::add(work[work[i].target], mark::superseded);
        }
    }
    // Steps 3 and 4: the leaves, the last of them current.
    State s;
    std::uint32_t leaves = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (live_version(i) && !detail::has(work[i], mark::superseded)) {
            ++leaves;
            s.current = static_cast<std::uint32_t>(i);
        }
    }
    if (leaves == 0) {
        return s;
    }
    s.forks = leaves - 1U;
    const ItemView& current = record[s.current];
    // Step 5.
    if (!current.held || detail::has(work[s.current], mark::forgotten)) {
        s.status = Status::forgotten;
        return s;
    }
    // Step 6: expires and now share at's fixed syntax, so text is time.
    if (!current.expires.empty() && current.expires <= now) {
        s.status = Status::expired;
        return s;
    }
    // Step 7: the first live check of current in the refuted row.
    for (std::size_t i = static_cast<std::size_t>(s.current) + 1U; i < n; ++i) {
        const ItemView& c = record[i];
        const bool live = c.held && !detail::has(work[i], mark::retired) && !detail::has(work[i], mark::forgotten);
        const bool mechanical = c.method == Method::test || c.method == Method::rerun ||
                                (c.method == Method::transcript && c.transcript == Transcript::reverified);
        if (c.kind == "check" && work[i].target == s.current && live && c.result == Result::refuted &&
            mechanical && !one_writer(c.writer, current.writer)) {
            s.status = Status::refuted;
            s.refuter = static_cast<std::uint32_t>(i);
            return s;
        }
    }
    s.status = Status::served;
    return s;
}

}  // namespace qiven3::entry::fold
