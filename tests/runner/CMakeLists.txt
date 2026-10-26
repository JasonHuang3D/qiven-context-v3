# qiven3_runner: the test runner and the main of every case binary, which
# counts the allocators of v3's memory a case takes after it.
add_library(qiven3_runner STATIC
  runner.cpp
  runner.hpp
)
target_include_directories(qiven3_runner PUBLIC "${PROJECT_SOURCE_DIR}/tests")
# Its main holds the process's log (support/log/journal.hpp), kept for the
# entry's report; a PlatformError escaping a case is named by its code
# (qiven3_platform).
target_link_libraries(qiven3_runner PUBLIC qiven3_memory qiven3_log PRIVATE qiven3_flags qiven3_diag
                      qiven3_platform)

# The runner's own cases, each a CTest test.
qiven3_add_cases(qiven3_runner_test LABELS fast SOURCES runner_test.cpp LINK qiven3_platform)

# One case per route, run as processes by tests/drive/test_runner.py; never
# a CTest test itself.
qiven3_executable(qiven3_runner_demo SOURCES runner_demo.cpp LINK qiven3_runner)
