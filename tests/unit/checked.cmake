# checked/rules' cases (the store's stage with the rules): check before any
# lock and admit inside it, on the fixtures over a view in memory: an
# append in two calls; a forget, never refused for size; forget_kind
# first, the parser's code, then part 1; forget_path; a content hit
# refused secret, admitted once cleared, refused under another clearing or
# once used; a header hit; part 2's first fault; the maximum by count and
# by size; at import part 1 alone, the final view's codes and
# head_mismatch; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_checked_stage LABELS fast fake
  SOURCES checked_stage_test.cpp checked_stage_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_checked qiven3_store)

# checked/clearing's cases (a false secret hit's clearing) through the
# ruled stores from the ruled factory, on the fake: a hit refused
# uncleared or cleared for another pattern, admitted on an imported state,
# then refused once used, once the clearing is forgotten and under a
# revision; over 4 and 16 direction records, append's counted peak equal,
# in memory and in SQLite.
qiven3_add_cases(qiven3_unit_checked_clearing LABELS fast fake
  SOURCES checked_clearing_test.cpp
  LINK qiven3_checked_stores)

# checked's rules through the ruled stores, the store's cases that test a
# rule of the entry's, on the fake: the store's widths against the entry's;
# a content hit refused secret at append; a forget of part of a record
# refused whole_record, then the whole forgotten; a ruled store's own
# export imported, empty and not, in memory and in SQLite.
qiven3_add_cases(qiven3_unit_checked_reads LABELS fast fake
  SOURCES checked_reads_test.cpp
  LINK qiven3_checked_stores)
