// faults' own case, faults.counts: run_faults' counts, the contract its
// consumers' cases rest on, over an operation of two calls of the countdown:
// a failure swallowed and another exception each counted untyped, a failed
// check counted unchecked, and the limit reached with no completion. Then a
// fault run's parts (qiven/faults/parts.hpp): a table of three parts over two
// passes and its ranges, held by the compiler; the registry's check, which
// refuses a part's case missing; and one pass run in its two parts, each run
// once.

#include <cstddef>
#include <cstdint>

#include "qiven/faults/faults.hpp"
#include "qiven/faults/parts.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"

namespace {

// Three parts: pass 0's k 1 to 40, its k from 41, and the whole of pass 1.
constexpr qiven::test::FaultParts<3> parts{{{{0, 1}, {0, 41}, {1, 1}}}};
static_assert(parts.whole());
static_assert(parts.range(0, 0).from == 1 && parts.range(0, 0).to == 40 && parts.range(0, 1).none());
static_assert(parts.range(1, 0).from == 41 && parts.range(1, 1).none() && parts.range(2, 0).none());
static_assert(parts.range(2, 1).from == 1 && parts.range(2, 1).to == SIZE_MAX);

#if defined(QIVEN_FAULTS)
// The operation's typed error, and another of its exceptions.
struct Typed {};
struct Other {};

using qiven::memory::Countdown;
using qiven::test::FaultRuns;
using qiven::test::run_faults;

// The calls of one pass run in parts: k 1 to 45 each fail.
constexpr std::size_t pass_calls = 45;

// One pass of pass_calls calls, typed, run in part's range.
FaultRuns part_runs(qiven::runner::Context& qiven_ctx, Countdown& countdown, qiven::test::PartRuns& part) {
    return run_faults<Typed>(
        qiven_ctx.log, "faults.parts", countdown,
        [&] {
            part.enter<Typed>(countdown);
            for (std::size_t c = 0; c < pass_calls; ++c) {
                if (countdown.fail()) {
                    throw Typed{};
                }
            }
        },
        [](std::size_t) { return true; });
}
#endif

}  // namespace

QIVEN_CASE(faults, counts) {
    // No part's case is registered here, so a table of one part is refused
    // and one of none held.
    CHECK(qiven::test::registered("faults.counts", 0));
    CHECK(!qiven::test::registered("faults.counts", parts.from.size()));
#if defined(QIVEN_FAULTS)
    Countdown countdown;
    // Each failure ends in the typed error; the check fails after k 2.
    FaultRuns r = run_faults<Typed>(
        qiven_ctx.log, "faults.typed", countdown,
        [&] {
            for (int c = 0; c < 2; ++c) {
                if (countdown.fail()) {
                    throw Typed{};
                }
            }
        },
        [](std::size_t k) { return k != 2; });
    CHECK_EQ(r.typed, 2U);
    CHECK_EQ(r.untyped, 0U);
    CHECK_EQ(r.unchecked, 1U);
    CHECK_EQ(r.completed_at, 3U);

    // Each failure swallowed: the operation returns.
    r = run_faults<Typed>(
        qiven_ctx.log, "faults.swallowed", countdown,
        [&] {
            static_cast<void>(countdown.fail());
            static_cast<void>(countdown.fail());
        },
        [](std::size_t) { return true; });
    CHECK_EQ(r.typed, 0U);
    CHECK_EQ(r.untyped, 2U);
    CHECK_EQ(r.unchecked, 0U);
    CHECK_EQ(r.completed_at, 3U);

    // Each failure ends in another exception.
    r = run_faults<Typed>(
        qiven_ctx.log, "faults.other", countdown,
        [&] {
            for (int c = 0; c < 2; ++c) {
                if (countdown.fail()) {
                    throw Other{};
                }
            }
        },
        [](std::size_t) { return true; });
    CHECK_EQ(r.typed, 0U);
    CHECK_EQ(r.untyped, 2U);
    CHECK_EQ(r.unchecked, 0U);
    CHECK_EQ(r.completed_at, 3U);

    // The limit, 2, reached before the operation's third call can complete.
    r = run_faults<Typed>(
        qiven_ctx.log, "faults.limit", countdown,
        [&] {
            for (int c = 0; c < 3; ++c) {
                if (countdown.fail()) {
                    throw Typed{};
                }
            }
        },
        [](std::size_t) { return true; }, 2);
    CHECK_EQ(r.typed, 2U);
    CHECK_EQ(r.untyped, 0U);
    CHECK_EQ(r.unchecked, 0U);
    CHECK_EQ(r.completed_at, 0U);

    // Part 1 executes k 1 to 40 and completes past its range, by k 41 run
    // disarmed; part 2 skips k 1 to 40, each run typed by its own failure,
    // and executes k 41 to the pass's own completion at 46.
    qiven::test::PartRuns first(parts.range(0, 0));
    r = part_runs(qiven_ctx, countdown, first);
    CHECK(first.held(r));
    CHECK(!first.completed_here(r));
    CHECK_EQ(first.executed(), 40U);
    CHECK_EQ(r.typed, 40U);
    CHECK_EQ(r.completed_at, 41U);
    qiven::test::PartRuns second(parts.range(1, 0));
    r = part_runs(qiven_ctx, countdown, second);
    CHECK(second.held(r));
    CHECK(second.completed_here(r));
    CHECK_EQ(second.before(), 40U);
    CHECK_EQ(second.executed(), pass_calls + 1 - second.before());
    CHECK_EQ(r.typed, pass_calls);
    CHECK_EQ(r.untyped, 0U);
    CHECK_EQ(r.completed_at, pass_calls + 1);
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
