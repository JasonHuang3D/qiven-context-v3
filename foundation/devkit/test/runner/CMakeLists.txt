# qiven_runner: the test runner, on v3's memory alone, which counts the
# allocators of v3's memory a case takes after it. The main of every case
# binary, which holds the process's log, is the log's (qiven3_case_main,
# tests/support/log); an error of another module escaping a case is named
# by the namer that module's cases register. Its public headers are
# include/qiven/runner/; tests/ stays on its consumers' include path, as
# before the move, until the rest of the test tree moves.
add_library(qiven_runner STATIC
  include/qiven/runner/core.hpp
  include/qiven/runner/runner.hpp
  src/runner.cpp
)
target_include_directories(qiven_runner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven_runner PUBLIC qiven3_memory PRIVATE qiven_build)

# The runner's own cases, each a CTest test, with the log's main.
qiven3_add_cases(qiven_runner_test LABELS fast SOURCES tests/runner_test.cpp LINK qiven3_platform)

# One case per route, run as processes by tests/drive/test_runner.py; never
# a CTest test itself.
qiven_executable(qiven_runner_demo SOURCES tests/runner_demo.cpp LINK qiven3_case_main)
