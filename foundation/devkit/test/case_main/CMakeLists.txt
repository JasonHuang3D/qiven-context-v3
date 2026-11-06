# qiven_case_main: the main of every case binary, the log's: the process's
# log, each case's case.end (its Ended, qiven/case_main/ended.hpp), the
# cases' registration (qiven/runner/runner.hpp) and a PlatformError's name,
# over the runner (qiven_runner).
add_library(qiven_case_main STATIC
  src/main.cpp
)
target_include_directories(qiven_case_main PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_case_main PUBLIC qiven_runner qiven_log PRIVATE qiven_build qiven_diag
                      qiven_platform)

# The case main's own cases, each a CTest test, with the main they test.
qiven_add_cases(qiven_case_main_test LABELS fast SOURCES tests/case_main_test.cpp LINK qiven_platform)
