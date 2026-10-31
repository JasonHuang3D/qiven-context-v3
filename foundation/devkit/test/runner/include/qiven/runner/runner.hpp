#pragma once

// The cases' registration, the log's: QIVEN_CASE(group, name) registers a
// case with the runner (qiven/runner/core.hpp) as "group.name", that name also
// the log's Name, made from the literal, for the case's case.end in the
// process's log (support/log/main.cpp: the main of every case binary,
// which holds the log, drains it into the output QIVEN3_LOG names, and
// names a PlatformError a case lets out).

#include <chrono>
#include <cstddef>

#include "log/log.hpp"
#include "qiven/runner/core.hpp"

namespace qiven3::test {

// The event case.end in log: the case's name, its outcome "pass", "fail" or
// "skip" by status, its milliseconds since start and held, the bytes its
// allocators held. The main's Ended, which runner_test gives run_case.
void case_ended(log::Log& log, const qiven::runner::Case& c, int status,
                std::chrono::steady_clock::time_point start, std::size_t held);

}  // namespace qiven3::test

#define QIVEN_CAT2(a, b) a##b
#define QIVEN_CAT(a, b) QIVEN_CAT2(a, b)

#define QIVEN_CASE_AS(group, name, serial)                                                    \
    static void QIVEN_CAT(qiven_case_, QIVEN_CAT(group, QIVEN_CAT(_, name)))(              \
        ::qiven::runner::Context & qiven_ctx);                                                 \
    static constexpr ::qiven3::log::Name QIVEN_CAT(qiven_name_,                              \
                                                    QIVEN_CAT(group, QIVEN_CAT(_, name))){   \
        #group "." #name};                                                                     \
    static const ::qiven::runner::Registrar QIVEN_CAT(                                         \
        qiven_reg_, QIVEN_CAT(group, QIVEN_CAT(_, name))){                                  \
        #group "." #name, &QIVEN_CAT(qiven_name_, QIVEN_CAT(group, QIVEN_CAT(_, name))),   \
        &QIVEN_CAT(qiven_case_, QIVEN_CAT(group, QIVEN_CAT(_, name))), serial};            \
    static void QIVEN_CAT(qiven_case_, QIVEN_CAT(group, QIVEN_CAT(_, name)))(              \
        [[maybe_unused]] ::qiven::runner::Context & qiven_ctx)

// QIVEN_CASE(group, name) { CHECK(...); }
#define QIVEN_CASE(group, name) QIVEN_CASE_AS(group, name, false)
// A timed case: CTest runs it with no other test beside it.
#define QIVEN_SERIAL_CASE(group, name) QIVEN_CASE_AS(group, name, true)
