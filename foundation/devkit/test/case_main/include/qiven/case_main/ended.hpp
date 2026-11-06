#pragma once

// The case main's Ended (qiven/runner/core.hpp), which main.cpp gives the
// runner and its own cases (tests/case_main_test.cpp) read.

#include <chrono>
#include <cstddef>

#include "qiven/log/log.hpp"
#include "qiven/runner/core.hpp"

namespace qiven::test {

// The event case.end in log: the case's name, its outcome "pass", "fail" or
// "skip" by status, its milliseconds since start and held, the bytes its
// allocators held.
void case_ended(qiven::log::Log& log, const qiven::runner::Case& c, int status,
                std::chrono::steady_clock::time_point start, std::size_t held);

}  // namespace qiven::test
