#pragma once

// The cases' registration, the log's: QIVEN3_CASE(group, name) registers a
// case with the runner (runner/core.hpp) as "group.name", that name also
// the log's Name, made from the literal, for the case's case.end in the
// process's log (support/log/main.cpp: the main of every case binary,
// which holds the log, drains it into the output QIVEN3_LOG names, and
// names a PlatformError a case lets out).

#include <chrono>
#include <cstddef>

#include "log/log.hpp"
#include "runner/core.hpp"

namespace qiven3::test {

// The event case.end in log: the case's name, its outcome "pass", "fail" or
// "skip" by status, its milliseconds since start and held, the bytes its
// allocators held. The main's Ended, which runner_test gives run_case.
void case_ended(log::Log& log, const Case& c, int status, std::chrono::steady_clock::time_point start,
                std::size_t held);

}  // namespace qiven3::test

#define QIVEN3_CAT2(a, b) a##b
#define QIVEN3_CAT(a, b) QIVEN3_CAT2(a, b)

#define QIVEN3_CASE_AS(group, name, serial)                                                    \
    static void QIVEN3_CAT(qiven3_case_, QIVEN3_CAT(group, QIVEN3_CAT(_, name)))(              \
        ::qiven3::test::Context & qiven3_ctx);                                                 \
    static constexpr ::qiven3::log::Name QIVEN3_CAT(qiven3_name_,                              \
                                                    QIVEN3_CAT(group, QIVEN3_CAT(_, name))){   \
        #group "." #name};                                                                     \
    static const ::qiven3::test::Registrar QIVEN3_CAT(                                         \
        qiven3_reg_, QIVEN3_CAT(group, QIVEN3_CAT(_, name))){                                  \
        #group "." #name, &QIVEN3_CAT(qiven3_name_, QIVEN3_CAT(group, QIVEN3_CAT(_, name))),   \
        &QIVEN3_CAT(qiven3_case_, QIVEN3_CAT(group, QIVEN3_CAT(_, name))), serial};            \
    static void QIVEN3_CAT(qiven3_case_, QIVEN3_CAT(group, QIVEN3_CAT(_, name)))(              \
        [[maybe_unused]] ::qiven3::test::Context & qiven3_ctx)

// QIVEN3_CASE(group, name) { CHECK(...); }
#define QIVEN3_CASE(group, name) QIVEN3_CASE_AS(group, name, false)
// A timed case: CTest runs it with no other test beside it.
#define QIVEN3_SERIAL_CASE(group, name) QIVEN3_CASE_AS(group, name, true)
