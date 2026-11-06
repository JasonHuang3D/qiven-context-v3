# Time's own tests: utc's vectors, evaluated by the compiler and again at
# run time, the zone test_utc.py sets by TZ, print_at's budget and its fault
# run; in one case binary, memory, whose arena and string the budget and
# fault cases take, reached through qiven_faults, so that it is on the link
# line once. Added by the runner's wiring.cmake, where qiven_add_cases,
# qiven_faults and qiven_case_main are made.
qiven_add_cases(qiven_time_test LABELS fast fake
  SOURCES utc_test.cpp
  LINK qiven_time qiven_faults)
