#pragma once

// fail: core's one fail-fast routine, for a broken internal invariant. A
// fault in the input is a typed error, never this.
//
// thread: fail_fast from any thread; the first cause kept stays.

#include <string_view>

namespace qiven3::memory {

// Ends the process at once, in every build, by std::terminate(): it
// allocates and writes nothing. `what` names the invariant; it is kept for
// the diagnostic entry's terminate handler, which reports it as the cause.
// `what` must outlive the process's end: a literal.
[[noreturn]] void fail_fast(std::string_view what) noexcept;

// The cause the first fail_fast kept, or empty when none ran.
[[nodiscard]] std::string_view fail_cause() noexcept;

}  // namespace qiven3::memory
