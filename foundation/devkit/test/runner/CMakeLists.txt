# qiven_runner: the test runner, on v3's memory, which counts the
# allocators of v3's memory a case takes after it. The main of every case
# binary, which holds the process's log, is the log's (qiven_case_main,
# foundation/devkit/test/case_main); an error of another module escaping a
# case is named by the namer that module's cases register. Its public
# headers are include/qiven/runner/; its registration header, runner.hpp,
# makes each case's Name of the log's, so it links the log; its core names
# the log's Log and Name, declared alone.
add_library(qiven_runner STATIC
  include/qiven/runner/core.hpp
  include/qiven/runner/runner.hpp
  src/runner.cpp
)
target_include_directories(qiven_runner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_runner PUBLIC qiven_memory qiven_log PRIVATE qiven_build)

# The runner's own cases, each a CTest test, with the log's main.
qiven_add_cases(qiven_runner_test LABELS fast SOURCES tests/runner_test.cpp)
