# Digest's own tests: SHA-256's and HMAC's vectors, keyed's text over a
# key's bytes and its wipe of the padded block, the comparison's, plain's
# budget and fault run, and the vectors evaluated by the compiler (the
# constant case); in one case binary, memory, whose arena and string the
# budget and fault cases take, reached through qiven_faults, so that it is on
# the link line once. Added by tests/CMakeLists.txt, where qiven3_add_cases,
# qiven_faults and qiven_case_main are made.
qiven3_add_cases(qiven_digest_test LABELS fast fake
  SOURCES digest_test.cpp digest_memory_test.cpp constant_test.cpp
  LINK qiven_digest qiven_faults)
