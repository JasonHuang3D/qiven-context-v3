# Format's own tests: the framing's and the id's vectors, keyed's text over
# a Key and the key's wipes on the fake platform, new_key's draw, the
# budgets and fault runs, and the vectors evaluated by the compiler (the
# constant case); in one case binary, memory, whose arena and string the
# budget and fault cases take, reached through qiven_faults, so that it is on
# the link line once. Added by tests/CMakeLists.txt, where qiven3_add_cases,
# qiven_faults and qiven_case_main are made.
qiven3_add_cases(qiven3_format_test LABELS fast fake
  SOURCES format_test.cpp format_memory_test.cpp constant_test.cpp
  LINK qiven3_format qiven_faults qiven_platform_fake)
