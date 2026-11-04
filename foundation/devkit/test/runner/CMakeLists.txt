# qiven_runner: the test runner, on v3's memory alone, which counts the
# allocators of v3's memory a case takes after it. The main of every case
# binary, which holds the process's log, is the log's (qiven_case_main,
# foundation/devkit/test/case_main); an error of another module escaping a
# case is named by the namer that module's cases register. Its public
# headers are include/qiven/runner/; its contract header declares the case
# main's case_ended over the log's Log, so it links the log.
add_library(qiven_runner STATIC
  include/qiven/runner/core.hpp
  include/qiven/runner/runner.hpp
  src/runner.cpp
)
target_include_directories(qiven_runner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_runner PUBLIC qiven_memory qiven_log PRIVATE qiven_build)

# The runner's own cases, each a CTest test, with the log's main.
qiven_add_cases(qiven_runner_test LABELS fast SOURCES tests/runner_test.cpp LINK qiven_platform)

# One case per route, run as processes by tests/drive/test_runner.py; never
# a CTest test itself.
qiven_executable(qiven_runner_demo SOURCES tests/runner_demo.cpp LINK qiven_case_main)
