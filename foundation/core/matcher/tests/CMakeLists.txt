# Matcher's own tests, over patterns it writes itself: the engine's vectors
# and compile's faults, evaluated by the compiler; the linear read, compile's
# budget and its fault run; in one case binary. Added by harness'
# wiring.cmake.
qiven_add_cases(qiven_matcher_test LABELS fast fake
  SOURCES matcher_test.cpp
  LINK qiven_matcher qiven_faults)
