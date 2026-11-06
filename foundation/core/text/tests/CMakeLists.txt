# Text's own tests: utf's, json's and canon's vectors and rules, their
# budgets and fault runs, and the vectors evaluated by the compiler (the
# constant case); in one case binary, qiven_text reached through the log
# qiven_faults links, so that it is on the link line once. Added by
# the runner's wiring.cmake.
qiven_add_cases(qiven_text_test LABELS fast fake
  SOURCES utf_test.cpp json_test.cpp json_memory_test.cpp canon_test.cpp canon_rules_test.cpp
    canon_memory_test.cpp constant_test.cpp
  LINK qiven_faults)
