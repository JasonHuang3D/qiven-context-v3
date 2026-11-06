# Diag's own tests: the ring's cases and the report's events, the kept
# rings' table; in one case binary, the ring and qiven_diag reached through
# the log and qiven_case_main, so that each is on the link line once; and the
# entry's planted faults (faults/), which test_faults.py runs. Added by
# the runner's wiring.cmake, where qiven_add_cases and qiven_case_main are made.
qiven_add_cases(qiven_diag_test LABELS fast fake SOURCES ring_test.cpp events_test.cpp)
# The report's events, diag's private header, by its name from diag's own src/.
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH qiven_diag_dir)
target_include_directories(qiven_diag_test PRIVATE "${qiven_diag_dir}/src")

add_subdirectory(faults)
