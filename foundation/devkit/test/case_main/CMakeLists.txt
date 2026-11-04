# qiven_case_main: the main of every case binary, the log's: the process's
# log, each case's case.end, the cases' registration (qiven/runner/runner.hpp)
# and a PlatformError's name, over the runner (qiven_runner).
add_library(qiven_case_main STATIC
  src/main.cpp
)
target_include_directories(qiven_case_main PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_case_main PUBLIC qiven_runner qiven_log PRIVATE qiven_build qiven_diag
                      qiven_platform)
