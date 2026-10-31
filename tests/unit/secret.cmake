# matcher's cases: the syntaxes' and patterns' vectors, the twelve patterns'
# fixtures, the linear read, compile's budget and fault run.
qiven3_add_cases(qiven3_unit_matcher LABELS fast fake
  SOURCES matcher_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_secret)

# secret's cases: the twelve patterns, a header hit refused with its
# pattern and path, each table and clearing fixture in a content string,
# English and Chinese text, no hit on the fixture entries' ids and digests
# nor on 4,000 fresh ones; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_secret LABELS fast fake
  SOURCES secret_test.cpp secret_memory_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_secret)
