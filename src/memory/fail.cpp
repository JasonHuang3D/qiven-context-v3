#include "memory/fail.hpp"

#include <exception>

namespace qiven3::memory {

void fail_fast(std::string_view what) noexcept {
    // `what` stays in this frame, unread, for a debugger or a dump.
    static_cast<void>(what);
    std::terminate();
}

}  // namespace qiven3::memory
