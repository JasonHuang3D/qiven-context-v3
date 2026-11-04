# Diag's own tests: the ring's cases and the report's events, the kept
# rings' table; in one case binary, qiven_diag reached through the log
# qiven_case_main links, so that it is on the link line once; and the
# entry's planted faults (faults/), which test_faults.py runs. Added by
# tests/CMakeLists.txt, where qiven_add_cases and qiven_case_main are made.
qiven_add_cases(qiven_diag_test LABELS fast fake SOURCES ring_test.cpp events_test.cpp)

add_subdirectory(faults)
