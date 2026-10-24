#include "memory/fail.hpp"

#include <atomic>
#include <exception>

namespace qiven3::memory {

namespace {

// The first fail_fast's cause, at namespace scope, so that the terminate
// handler reads it with nothing allocated: claimed once, written, then
// published; never cleared.
std::atomic_flag cause_claimed;
std::string_view cause;
std::atomic<bool> cause_ready{false};

}  // namespace

void fail_fast(std::string_view what) noexcept {
    if (!cause_claimed.test_and_set(std::memory_order_acq_rel)) {
        cause = what;
        cause_ready.store(true, std::memory_order_release);
    }
    std::terminate();
}

std::string_view fail_cause() noexcept {
    return cause_ready.load(std::memory_order_acquire) ? cause : std::string_view();
}

}  // namespace qiven3::memory
