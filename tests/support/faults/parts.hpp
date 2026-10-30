#pragma once

// A case's fault runs in parts, each part its own case, so that each stays
// within 30 s on every job. A fault point is a pass of the case, by its
// place in the case's order of passes, and a k within it. A table of the
// parts' first points, from pass 0's k 1 and rising: part i runs the points
// from its own first one up to the next part's, the last part to the end of
// the case's last pass (FaultParts::range gives each pass's k a part runs).
//
// Within a part, a pass runs by PartRuns: a run before the part's first k
// fails at once by its own k calls of the countdown, executing nothing; a
// run past its last k executes with the countdown disarmed, so that it
// completes, failing nothing, and ends the pass as its completing run
// does. A pass that completes within the part completes there; one that
// completes before the part's first k completes at it, that one run
// executed.
//
// Together the parts run each fault point once: the table rises from pass
// 0's k 1 (FaultParts::whole(), held by a static_assert where the table is
// given), each pass of a part executes each run of its range up to the
// completing one (PartRuns::held), and each part checks that its binary
// registers every part's case, "<base>_part<i>" for i from 1, and none
// past them (registered()), so that a part's case dropped is refused.
//
// thread: one

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace qiven3::test {

// A fault point: a pass, by its place in the case's order, and its k.
struct FaultPoint {
    std::size_t pass = 0;
    std::size_t k = 1;
};

// The k a part runs of one pass, from to to; to SIZE_MAX, to the run that
// completes; none when to is under from.
struct FaultRange {
    std::size_t from = 1;
    std::size_t to = SIZE_MAX;
    [[nodiscard]] constexpr bool none() const noexcept { return to < from; }
};

template <std::size_t N>
struct FaultParts {
    static_assert(N > 0);
    std::array<FaultPoint, N> from;

    // Part part's k of pass pass: from its first point, or 1, to before the
    // next part's first point, or to the completing run.
    [[nodiscard]] constexpr FaultRange range(std::size_t part, std::size_t pass) const {
        const FaultPoint start = from.at(part);
        if (pass < start.pass) {
            return {1, 0};
        }
        FaultRange r{pass == start.pass ? start.k : 1, SIZE_MAX};
        if (part + 1 < N) {
            const FaultPoint end = from.at(part + 1);
            if (pass > end.pass) {
                return {1, 0};
            }
            if (pass == end.pass) {
                r.to = end.k - 1;
            }
        }
        return r;
    }

    // Whether the parts run each fault point once: the first at pass 0's
    // k 1, each next one past the one before it, each k from 1.
    [[nodiscard]] constexpr bool whole() const {
        if (from.at(0).pass != 0 || from.at(0).k != 1) {
            return false;
        }
        for (std::size_t i = 1; i < N; ++i) {
            const FaultPoint a = from.at(i - 1);
            const FaultPoint b = from.at(i);
            if (b.k == 0 || !(a.pass < b.pass || (a.pass == b.pass && a.k < b.k))) {
                return false;
            }
        }
        return true;
    }
};

// Whether the binary registers the cases "<base>_part<i>" for i from 1 to
// n, and none for n + 1.
inline bool registered(std::string_view base, std::size_t n) {
    for (std::size_t i = 1; i <= n + 1; ++i) {
        const std::string name = std::string(base) + "_part" + std::to_string(i);
        const bool found = std::any_of(registry().begin(), registry().end(),
                                       [&name](const Case& c) { return c.name == name; });
        if (found != (i <= n)) {
            return false;
        }
    }
    return true;
}

#if defined(QIVEN3_FAULTS)

// One pass's runs within a part's range.
class PartRuns {
public:
    explicit PartRuns(FaultRange range) noexcept : range_(range) {}

    // At a run's start. A run before from fails at once by its own k calls
    // of the countdown, throwing Typed; a run past to disarms the countdown
    // and executes, completing. Each run from from to to is counted.
    template <typename Typed>
    void enter(memory::Countdown& countdown) {
        ++run_;
        if (run_ < range_.from) {
            for (std::size_t i = 0; i < run_; ++i) {
                static_cast<void>(countdown.fail());
            }
            throw Typed{};
        }
        if (run_ > range_.to) {
            countdown.disarm();
            return;
        }
        ++executed_;
    }
    // Whether the run was before from, executing nothing.
    [[nodiscard]] bool skipped() const noexcept { return run_ < range_.from; }
    [[nodiscard]] std::size_t executed() const noexcept { return executed_; }
    // The runs before from, each typed by its failure alone.
    [[nodiscard]] std::size_t before() const noexcept { return range_.from - 1; }
    // The last k the part ran: the completing run's, or to.
    [[nodiscard]] std::size_t last(const FaultRuns& runs) const noexcept {
        return std::min(runs.completed_at, range_.to);
    }
    // Whether the pass's own completion came within the range, not by a
    // run past to: a check of the whole pass's end holds there alone.
    [[nodiscard]] bool completed_here(const FaultRuns& runs) const noexcept {
        return runs.completed_at >= range_.from && runs.completed_at <= range_.to;
    }
    // Whether the pass ran its range: it completed at from or after, by
    // to + 1 at the latest, and each run from from to the completing one,
    // or to to, executed.
    [[nodiscard]] bool held(const FaultRuns& runs) const noexcept {
        const bool in = runs.completed_at >= range_.from &&
                        (range_.to == SIZE_MAX || runs.completed_at <= range_.to + 1);
        return in && executed_ == last(runs) - range_.from + 1;
    }

private:
    FaultRange range_;
    std::size_t run_ = 0;
    std::size_t executed_ = 0;
};

#endif

}  // namespace qiven3::test

// A part's case, "<group>.<name>_part<i>", i from 1: fn(qiven3_ctx, i - 1).
#define QIVEN3_FAULT_PART(group, name, i, fn) \
    QIVEN3_CASE(group, name##_part##i) { fn(qiven3_ctx, std::size_t{i} - 1); }
