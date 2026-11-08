#pragma once

// The cases' registration, the log's: QIVEN_CASE(group, name) registers a
// case with the runner (qiven/runner/core.hpp) as "group.name", that name also
// the log's Name, made from the literal, for the case's case.end in the
// process's log (qiven_case_main's main.cpp: the main of every case binary,
// which holds the log, drains it into the output QIVEN_LOG names, and
// names a PlatformError a case lets out).

#include "qiven/log/log.hpp"
#include "qiven/runner/core.hpp"

#define QIVEN_CAT2(a, b) a##b
#define QIVEN_CAT(a, b) QIVEN_CAT2(a, b)

// QIVEN_CASE(group, name) { CHECK(...); }
#define QIVEN_CASE(group, name)                                                               \
    static void QIVEN_CAT(qiven_case_, QIVEN_CAT(group, QIVEN_CAT(_, name)))(              \
        ::qiven::runner::Context & qiven_ctx);                                                 \
    static constexpr ::qiven::log::Name QIVEN_CAT(qiven_name_,                              \
                                                    QIVEN_CAT(group, QIVEN_CAT(_, name))){   \
        #group "." #name};                                                                     \
    static const ::qiven::runner::Registrar QIVEN_CAT(                                         \
        qiven_reg_, QIVEN_CAT(group, QIVEN_CAT(_, name))){                                  \
        #group "." #name, &QIVEN_CAT(qiven_name_, QIVEN_CAT(group, QIVEN_CAT(_, name))),   \
        &QIVEN_CAT(qiven_case_, QIVEN_CAT(group, QIVEN_CAT(_, name)))};                    \
    static void QIVEN_CAT(qiven_case_, QIVEN_CAT(group, QIVEN_CAT(_, name)))(              \
        [[maybe_unused]] ::qiven::runner::Context & qiven_ctx)
