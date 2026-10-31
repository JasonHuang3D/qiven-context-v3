# qiven3_case_main: the main of every case binary, the log's: the process's
# log, each case's case.end, the cases' registration (qiven/runner/runner.hpp)
# and a PlatformError's name, over the runner (qiven_runner).
add_library(qiven3_case_main STATIC
  main.cpp
)
target_link_libraries(qiven3_case_main PUBLIC qiven_runner qiven3_log PRIVATE qiven_build qiven3_diag
                      qiven3_platform)
