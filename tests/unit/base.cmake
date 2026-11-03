qiven3_add_cases(qiven3_unit_text LABELS fast fake
  SOURCES utf_test.cpp json_test.cpp json_memory_test.cpp canon_test.cpp canon_rules_test.cpp
    canon_memory_test.cpp constant_test.cpp
  LINK qiven_faults)
# The fixtures' filled.hpp by its path under tests/, which qiven_faults gave before its move.
target_include_directories(qiven3_unit_text PRIVATE "${PROJECT_SOURCE_DIR}/tests")

# digest's cases: its vectors, its key's wipes on the fake platform, its
# budgets and fault runs; and tests/fixtures/ through support/fixtures,
# each entry's digests and id.
qiven3_add_cases(qiven3_unit_digest LABELS fast fake
  SOURCES digest_test.cpp digest_memory_test.cpp
  LINK qiven_faults qiven_platform_fake)
