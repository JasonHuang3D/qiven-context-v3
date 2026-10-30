# qiven3_case_main: the main of every case binary, the log's: the process's
# log, each case's case.end, the cases' registration (runner/runner.hpp)
# and a PlatformError's name, over the runner (qiven3_runner).
add_library(qiven3_case_main STATIC
  main.cpp
  "${PROJECT_SOURCE_DIR}/tests/runner/runner.hpp"
)
target_link_libraries(qiven3_case_main PUBLIC qiven3_runner qiven3_log PRIVATE qiven3_flags qiven3_diag
                      qiven3_platform)

# The runner's own cases, each a CTest test, with the log's main.
qiven3_add_cases(qiven3_runner_test LABELS fast SOURCES "${PROJECT_SOURCE_DIR}/tests/runner/runner_test.cpp"
                 LINK qiven3_platform)

# One case per route, run as processes by tests/drive/test_runner.py; never
# a CTest test itself.
qiven3_executable(qiven3_runner_demo SOURCES "${PROJECT_SOURCE_DIR}/tests/runner/runner_demo.cpp"
                  LINK qiven3_case_main)
