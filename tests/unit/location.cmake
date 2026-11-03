# location's cases: the commands on the fake over a scratch directory; its
# smoke case: the command line, init warned then recorded, store_dir,
# chosen, show, change to the same directory, then not_moved; each usage
# error; the record and its refused forms, not_chosen, the guard, the
# warnings, init's and change's rules, ASCII output, the lock held; the
# budgets, counts and fault runs of each command and of store_dir, over
# support/fixtures/location_bed.hpp's steps.
qiven3_add_cases(qiven3_unit_location LABELS fast fake
  SOURCES location_test.cpp location_cases_test.cpp location_memory_test.cpp location_faults_test.cpp
  LINK qiven_faults qiven_platform_fake qiven3_location)
# The fixtures' location_bed.hpp by its path under tests/, which the fake gave before its move.
target_include_directories(qiven3_unit_location PRIVATE "${PROJECT_SOURCE_DIR}/tests")
