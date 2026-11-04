# secret's cases: the twelve patterns, a header hit refused with its
# pattern and path, each table and clearing fixture in a content string,
# English and Chinese text, no hit on the fixture entries' ids and digests
# nor on 4,000 fresh ones; its budgets and fault runs; each of the twelve
# patterns compiled alone on its fixtures; and the syntaxes' vectors, the
# entry's, until the entry is rebuilt.
qiven_add_cases(qiven3_unit_secret LABELS fast fake
  SOURCES secret_test.cpp secret_memory_test.cpp secret_matcher_test.cpp
  LINK qiven_faults qiven3_fixtures qiven3_secret)
