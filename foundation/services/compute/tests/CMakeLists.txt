# Compute's own tests, over its fake and the devices, sizes and kernel they
# write themselves: H1 to H5, the caller's arena and its fault run, in one
# case binary; and the caller's mistake's demo (H8), which test_fail.py runs.
# Added by the runner's wiring.cmake.
qiven_add_cases(qiven_compute_test LABELS fast fake
  SOURCES compute_test.cpp
  LINK qiven_compute_fake qiven_faults)

qiven_executable(qiven_compute_fail_demo SOURCES fail_demo.cpp LINK qiven_compute_fake qiven_case_main)
