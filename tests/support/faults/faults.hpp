#pragma once

// Fault injection's loop: an operation run with its k-th allocation or
// platform call failed, for k from 1 until it completes. Each run must end
// in the operation's typed error, after which the caller's check reads the
// count (the allocators holding nothing) and, from package 5, the store.
// Built without QIVEN3_FAULTS there is no countdown, and a case says so as
// not applicable.
//
// thread: one

#include <cstddef>

#include "memory/memory.hpp"

#if defined(QIVEN3_FAULTS)

namespace qiven3::test {

struct FaultRuns {
    // The runs whose failed call ended the operation in the typed error.
    std::size_t typed = 0;
    // The runs that ended otherwise: another exception, or a return with
    // the failure swallowed.
    std::size_t untyped = 0;
    // The runs after which the caller's check failed.
    std::size_t unchecked = 0;
    // The k at which the operation first completed, no call failing; 0 if
    // it never did within the limit.
    std::size_t completed_at = 0;
};

// Runs op() with the countdown armed at k = 1, 2, ... until a run makes
// fewer than k calls and so completes, or k passes limit. After each run,
// check(k) gives whether the count and the state are as before. Error is
// the operation's typed error.
template <typename Error, typename Op, typename Check>
FaultRuns run_faults(memory::Countdown& countdown, Op&& op, Check&& check,
                     std::size_t limit = 100000) {
    FaultRuns runs;
    for (std::size_t k = 1; k <= limit; ++k) {
        countdown.arm(k);
        bool typed = false;
        bool other = false;
        try {
            op();
        } catch (const Error&) {
            typed = true;
        } catch (...) {
            other = true;
        }
        const bool fired = countdown.fired();
        countdown.disarm();
        if (!check(k)) {
            ++runs.unchecked;
        }
        if (!fired && !typed && !other) {
            runs.completed_at = k;
            return runs;
        }
        if (fired && typed) {
            ++runs.typed;
        } else {
            ++runs.untyped;
        }
    }
    return runs;
}

}  // namespace qiven3::test

#endif
