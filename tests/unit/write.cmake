# write/entries over synthetic entries files: a write file and an import
# file read whole, each kept past the bytes, the holes and pieces found and
# filled; each form refusal naming its path and entry, and the content
# check's faults as invalid, under the content's path; the work span given
# back on every end, and each allocation failed in turn.
qiven3_add_cases(qiven3_unit_write_entries LABELS fast fake
  SOURCES write_entries_test.cpp write_entries_memory_test.cpp
  LINK qiven3_faults qiven3_write)

# write/build over synthetic files and a fake fold: a write file's chain,
# keys, holes, piece digests and records, revisions of the fold's current
# versions and at raised past them, an import file's chain per scope, a
# path piece read and its bound, and each hole or target refused by path;
# a forget, a clause's, a map's and a lane's checkpoint's revisions, a
# question root, expires against the real at; out empty on every error,
# each allocation and each platform call failed in turn; 512 entries in a
# fixed arena, a path piece read once, an @name: of another scope refused.
qiven3_add_cases(qiven3_unit_write_build LABELS fast fake
  SOURCES write_build_test.cpp write_build_memory_test.cpp write_build_span_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_write)

# write/refuse over synthetic files read, built and refused against a fake
# fold: each refusal code of the write command with its cases and its
# nearest pass, the path each names, the order of the codes, a session
# revising the review's claimed entries, and the fold's queries counted.
qiven3_add_cases(qiven3_unit_write_refuse LABELS fast fake
  SOURCES write_refuse_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_write)

# write/kept over a scratch store's real files on the fake and a fake
# store's ids (support/fixtures/kept_bed.hpp): the kept file's form, names
# and corrupt forms; writes kept in order under the lock; each step of the
# kept files with its faults, the pending file whole or absent at each and
# settled by ids on the next write; a rename meeting a file under its new
# name; busy under a held lock; the import's file with no lock and no
# settle step; each allocation and each platform call failed in turn over
# a write, a refused write and the import, settled by the next write.
qiven3_add_cases(qiven3_unit_write_kept LABELS fast fake
  SOURCES write_kept_test.cpp write_kept_steps_test.cpp write_kept_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_write)
