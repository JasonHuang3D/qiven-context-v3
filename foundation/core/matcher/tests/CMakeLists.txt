# Matcher's own tests: the engine's vectors and compile's faults, evaluated
# by the compiler, in one case binary. Added by tests/CMakeLists.txt, where
# qiven3_add_cases and qiven_case_main are made.
qiven3_add_cases(qiven_matcher_test LABELS fast fake
  SOURCES matcher_test.cpp
  LINK qiven_matcher)
