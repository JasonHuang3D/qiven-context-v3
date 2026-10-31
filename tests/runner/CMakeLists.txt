# qiven3_runner: the test runner, on v3's memory alone, which counts the
# allocators of v3's memory a case takes after it. The main of every case
# binary, which holds the process's log, is the log's (qiven3_case_main,
# support/log); an error of another module escaping a case is named by the
# namer that module's cases register.
add_library(qiven3_runner STATIC
  core.hpp
  runner.cpp
)
target_include_directories(qiven3_runner PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_runner PUBLIC qiven3_memory PRIVATE qiven_build)
