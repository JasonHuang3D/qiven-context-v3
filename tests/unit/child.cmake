# win/process' cases on the Windows adapter, on Windows alone: program;
# start's refusals; each argument, the environment, the input, wait, kill
# and the tree's descendants through qiven3-child platform; read_line, the
# caps, the deadline and run; the heap counted by the binary's own
# operator new, the jthreads' blocks included; each Win32 call and arena
# allocation failed in turn. machine: they start processes, so the full
# and asan presets run them, not the fast or fake ones.
if(WIN32)
  qiven3_add_cases(qiven3_unit_win_process LABELS machine
    SOURCES win_process_test.cpp win_process_streams_test.cpp win_process_memory_test.cpp
            win_process_faults_test.cpp
    LINK qiven_faults qiven_platform_windows)
  # The fixtures' win_process_case.hpp by its path under tests/, which qiven_faults gave before its move.
  target_include_directories(qiven3_unit_win_process PRIVATE "${PROJECT_SOURCE_DIR}/tests")
  add_dependencies(qiven3_unit_win_process qiven3-child)
endif()
