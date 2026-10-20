#pragma once

// fail: core's one fail-fast routine, for a broken internal invariant. A
// fault in the input is a typed error, never this.
//
// thread: none

#include <string_view>

namespace qiven3::memory {

// Ends the process at once, in every build, by std::terminate(): it
// allocates and writes nothing. `what` names the invariant, for a debugger
// or a dump.
[[noreturn]] void fail_fast(std::string_view what) noexcept;

}  // namespace qiven3::memory
