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
