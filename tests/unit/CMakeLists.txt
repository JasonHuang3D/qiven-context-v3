# The unit cases of v3's memory: memory, arena, pool and its frees, each case
# its own CTest test; the cases of utf, json and canon, with unit/constant's
# vectors, which the compiler evaluates; fail_fast's demo, which
# tests/drive/test_fail.py runs; and the frees that end the process, which
# tests/drive/test_free.py runs.
qiven3_add_cases(qiven3_unit_memory LABELS fast fake
  SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp
  LINK qiven3_faults)

qiven3_add_cases(qiven3_unit_text LABELS fast fake
  SOURCES utf_test.cpp json_test.cpp json_memory_test.cpp canon_test.cpp canon_rules_test.cpp
    canon_memory_test.cpp constant_test.cpp
  LINK qiven3_faults)

qiven3_executable(qiven3_fail_demo SOURCES fail_demo.cpp LINK qiven3_runner)

# The frees that end the process, which tests/drive/test_free.py runs.
qiven3_executable(qiven3_free_demo SOURCES free_demo.cpp LINK qiven3_runner)

# Under AddressSanitizer with QIVEN3_SYSTEM_HEAP, memory's own code, the
# runner over it, memory's cases and the frees' demo are built a second time
# without the option: the blocks are the span's own, which the allocators
# poison but for what they hand out (memory/poison.hpp), so the code that
# places a block runs under the sanitizer. tests/drive/test_free.py runs
# both binaries; every other target keeps the system heap.
if(QIVEN3_SYSTEM_HEAP AND QIVEN3_SANITIZE MATCHES "address")
  get_target_property(qiven3_memory_sources qiven3_memory SOURCES)
  list(TRANSFORM qiven3_memory_sources PREPEND "${PROJECT_SOURCE_DIR}/src/memory/")
  add_library(qiven3_memory_poisoned STATIC ${qiven3_memory_sources})
  target_include_directories(qiven3_memory_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(qiven3_memory_poisoned PRIVATE qiven3_flags)

  # The entry's targets, and the base and the log the runner's log takes,
  # over the poisoned memory, so that one copy of memory is linked.
  qiven3_diag_targets(_poisoned qiven3_memory_poisoned)
  foreach(pair IN ITEMS "qiven3_base;qiven3;qiven3_memory_poisoned"
                        "qiven3_log;log;qiven3_base_poisoned;qiven3_memory_poisoned")
    list(POP_FRONT pair name dir)
    get_target_property(sources ${name} SOURCES)
    list(TRANSFORM sources PREPEND "${PROJECT_SOURCE_DIR}/src/${dir}/")
    add_library(${name}_poisoned STATIC ${sources})
    target_include_directories(${name}_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${name}_poisoned PUBLIC ${pair} PRIVATE qiven3_flags)
  endforeach()

  add_library(qiven3_runner_poisoned STATIC "${PROJECT_SOURCE_DIR}/tests/runner/runner.cpp")
  target_include_directories(qiven3_runner_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/tests")
  target_link_libraries(qiven3_runner_poisoned PUBLIC qiven3_memory_poisoned qiven3_log_poisoned
    PRIVATE qiven3_flags qiven3_diag_poisoned)
  qiven3_executable(qiven3_unit_memory_poisoned
    SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  qiven3_executable(qiven3_free_demo_poisoned SOURCES free_demo.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  set_target_properties(qiven3_memory_poisoned qiven3_base_poisoned qiven3_log_poisoned
    qiven3_runner_poisoned qiven3_unit_memory_poisoned
    qiven3_free_demo_poisoned qiven3_diag_poisoned qiven3_diag_entry_poisoned PROPERTIES QIVEN3_POISONED ON)
endif()

# digest's cases: its vectors, its key's wipes on the fake platform, its
# budgets and fault runs; and tests/fixtures/ through support/fixtures,
# each entry's digests and id.
qiven3_add_cases(qiven3_unit_digest LABELS fast fake
  SOURCES digest_test.cpp digest_memory_test.cpp digest_fixtures_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures)

# utc's cases: its vectors, L1's 8-hour shift through support/fixtures,
# the zone tests/drive/test_utc.py sets by TZ, its budget and fault run.
qiven3_add_cases(qiven3_unit_utc LABELS fast fake
  SOURCES utc_test.cpp
  LINK qiven3_faults qiven3_fixtures)

# The log's cases: log, ring, the report's events, the static half's, and
# K13's binary half, whose binary tests/drive/test_log.py reads.
qiven3_add_cases(qiven3_unit_log LABELS fast fake
  SOURCES log_test.cpp log_static.cpp log_k13.cpp ring_test.cpp events_test.cpp
  LINK qiven3_faults)

# K13's planted field (log_plant.cpp): the control in every build, the plant
# built by its test alone, never by the build, the analysis or the records.
add_library(qiven3_log_plant_control OBJECT log_plant.cpp)
add_library(qiven3_log_plant OBJECT EXCLUDE_FROM_ALL log_plant.cpp)
target_compile_definitions(qiven3_log_plant PRIVATE QIVEN3_PLANT_FIELD)
foreach(target IN ITEMS qiven3_log_plant_control qiven3_log_plant)
  target_link_libraries(${target} PRIVATE qiven3_flags qiven3_log)
endforeach()
set_target_properties(qiven3_log_plant PROPERTIES QIVEN3_PLANT ON EXPORT_COMPILE_COMMANDS OFF)

# matcher's cases: the syntaxes' and patterns' vectors, the twelve patterns'
# fixtures, the linear read, compile's budget and fault run.
qiven3_add_cases(qiven3_unit_matcher LABELS fast fake
  SOURCES matcher_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_secret qiven3_matcher)

# secret's cases: the twelve patterns, a header hit refused with its
# pattern and path, each table and clearing fixture in a content string,
# English and Chinese text, no hit on the fixture entries' ids and digests
# nor on 4,000 fresh ones; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_secret LABELS fast fake
  SOURCES secret_test.cpp secret_memory_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_secret)

# header's cases: A's refusals and the codes' order, the syntaxes, make and
# line, stamp, evaluated by the compiler and again at run time over
# tests/fixtures/; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_header LABELS fast fake
  SOURCES header_test.cpp header_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_entry)

# body's cases: the record's layout and codes in order, written and read at
# their places, evaluated by the compiler; every fixture body made, written,
# read back and checked against its header; the refusals' body rows; the
# key's hex; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_body LABELS fast fake
  SOURCES body_test.cpp body_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_entry)

# kinds' cases: the tables against header's closed sets, the fields and
# limits, when and pins, a direction's clears, the common fields' carriage
# and record identity, evaluated by the compiler; identity over every
# fixture entry; no allocation, counted and under the fault run.
qiven3_add_cases(qiven3_unit_kinds LABELS fast fake
  SOURCES kinds_test.cpp kinds_memory_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_entry)

# refs' cases: the code references' vectors, the whitespace table, each
# type of reference and each code in order, evaluated by the compiler; the
# limits at their bounds; every reference, subject and path of the
# fixtures; the compiles counted, under the budget and the fault run.
qiven3_add_cases(qiven3_unit_refs LABELS fast fake
  SOURCES refs_test.cpp refs_memory_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_entry)

# content's cases: each code on its field and the first in the table's
# order, basis and scope leading, the admission test, exact key sets, when
# and pins, evaluated by the compiler where no syntax is compiled; at run
# time the limits at their bounds, a lesson of 500 Chinese characters and
# of 501, references, a map's object, the episode, the kinds' rules, the
# size, every fixture entry, a usage at every limit of 50,698 bytes; the
# compiles counted, under the budget and the fault run.
qiven3_add_cases(qiven3_unit_content LABELS fast fake
  SOURCES content_test.cpp content_memory_test.cpp content_usage_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_entry)

# check's cases: part 1's order, the parser's, the header's, the record's,
# its binding and the content's, a header with no body meeting basis and
# scope alone, evaluated by the compiler; every fixture entry with its
# record and with none, the refusals of A, the parser's on the line and the
# record, a content past 65,536 bytes; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_check LABELS fast fake
  SOURCES check_test.cpp check_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_entry)

# validator's cases: part 2's codes against a view in memory, each on its
# entry and the first deciding, by the code's order within an entry, then
# entry by entry, the call's earlier entries held, evaluated by the
# compiler; at run time the fixtures' lanes appended in turn, E's forget
# among them, then a revision of B forgotten_target, a second forget, a
# map's revision changing its object, a decision citing M1 and references
# out of scope and not held; its budgets and fault runs. At import, the
# chain rules as each line arrives and part 2's other rules in the final
# view, a second forget and a whole record among them, evaluated by the
# compiler; the fixtures imported whole, their budgets and fault runs.
qiven3_add_cases(qiven3_unit_validator LABELS fast fake
  SOURCES validator_test.cpp validator_memory_test.cpp validator_import_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_entry)

# fold's cases: the order, one writer, each step of the fold ending it in
# turn (F50, F51, F118) and "active", evaluated by the compiler; the
# fold's faults; FA2's status change and the scope's order over the
# fixtures; its one allocation counted, under the budget and the fault run.
qiven3_add_cases(qiven3_unit_fold LABELS fast fake
  SOURCES fold_test.cpp fold_memory_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_entry)

# labels' cases: the base by basis and an owner-words version's bindings
# (F29, F120), the strength rows and their precedence, the own writer's,
# a dead and an unequal check, a reflection writer's lesson lifted (F156,
# A10) and the longest labels, evaluated by the compiler; a label past its
# capacity; FA2's model entry over the fixtures, with a stub transcript
# reader; no allocation, under the budget and the fault run.
qiven3_add_cases(qiven3_unit_labels LABELS fast fake
  SOURCES labels_test.cpp labels_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_entry)

# served's cases: a block's lines, its text escaped onto one line, its state
# line, its fields' forms and a deeper value's canonical JSON, a reflection
# writer's lesson as a claim until lifted, a stub, cost, map_mark and the
# budgets, evaluated by the compiler; over the fixtures M1 stale and M2
# fresh at 387 and 402 units, L1 and L2's when and pins, L1 superseded,
# MX's block of 42,658 units and its stub of 201, and the refusals; one
# allocation each for block and stub, under the budget and the fault run.
qiven3_add_cases(qiven3_unit_served LABELS fast fake
  SOURCES served_test.cpp served_memory_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_entry)

# projectmap's cases: the map files' syntaxes, os.path.isabs on each
# system, the registry and the marks file read with each code, the marks
# file written back the same, Marks built and refused, the map's rule
# against a view and an entry reference's digest, evaluated by the
# compiler; FA2's map case, the four marks, three digests and two ids over
# the fixtures; each operation's allocations counted, under the budget and
# the fault run.
qiven3_add_cases(qiven3_unit_projectmap LABELS fast fake
  SOURCES projectmap_test.cpp projectmap_memory_test.cpp map_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_entry)

# FA5's vectors case: tests/vectors/ids.json read whole, every vector
# recomputed by canon, digest, header, body, check, content, projectmap,
# fold, labels and served, the fixtures' entries giving its ids, none left
# unread; each vector planted wrong and the file's own plants caught.
qiven3_add_cases(qiven3_unit_vectors LABELS fast fake
  SOURCES vectors_test.cpp vectors_read.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_entry)

# files' cases on the fake: its pure checks evaluated by the compiler;
# replace, stage and lock on real files in each case's scratch directory,
# the fake's busy counts driving the retries (P9, P19, P20's lock, P25's
# replace and open); each system and platform call failed in turn.
qiven3_add_cases(qiven3_unit_files LABELS fast fake
  SOURCES files_test.cpp
  LINK qiven3_faults qiven3_platform_fake)

# store/errors' cases: each kind with its fields and its code, `secret`
# with no id, the cuts at a code point and the names, evaluated by the
# compiler; the widths against the entry's; each kind's line; an error
# thrown and caught; the fault run over none.
qiven3_add_cases(qiven3_unit_store_errors LABELS fast fake
  SOURCES store_errors_test.cpp
  LINK qiven3_faults qiven3_secret qiven3_entry qiven3_store)

# store/codes' cases: the table's named numbers, its order and its names;
# each row of the mapping and each purge reason, evaluated by the compiler;
# every code at every place and step; a line; the fault run over none.
qiven3_add_cases(qiven3_unit_store_codes LABELS fast fake
  SOURCES store_codes_test.cpp
  LINK qiven3_faults qiven3_store)

# store/contract's cases: a minimal store, scan and export the concepts
# accept and a plant of each that they refuse; the names, read-only's
# operations, the kinds each raises, the capabilities' applicability, the
# limits and volume and the budgets, evaluated by the compiler; the minimal store
# driven through the concept; the gate's refusals in order; a line on the
# caller's arena; the fault run over none.
qiven3_add_cases(qiven3_unit_store_contract LABELS fast fake
  SOURCES store_contract_test.cpp
  LINK qiven3_faults qiven3_store)

# store/stage's cases: check before any lock and admit inside it, on the
# fixtures over a view in memory: an append in two calls; a forget, never
# refused for size; forget_kind first, the parser's code, then part 1;
# forget_path; a content hit refused secret, admitted once cleared, refused
# under another clearing or once used; a header hit; part 2's first fault;
# the maximum by count and by size; at import part 1 alone, the final
# view's codes and head_mismatch; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_store_stage LABELS fast fake
  SOURCES store_stage_test.cpp store_stage_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_store)

# store/memory's cases: the in-memory store over one state on the fake;
# its smoke and exchange cases, its budgets, counts and fault runs, its
# lock, thread gate, read-only object and checked reads; a scan's end at
# each lane's length at its start (store_scan_case.hpp).
qiven3_add_cases(qiven3_unit_store_memory LABELS fast fake
  SOURCES store_memory_test.cpp store_memory_memory_test.cpp store_memory_threads_test.cpp
          store_memory_scan_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_store)

# store/sqlite's cases: the SQLite store on the fake over a scratch
# directory; its smoke case: create, identify, the settings read back, the
# files open refuses, the guard and coherence; its writes case: append,
# forget, purge, size and close; its reads; its budgets, counts and fault
# runs; the engine heap's page cache, a purge held pending, forged files;
# a scan's end at each lane's length at its start (store_scan_case.hpp);
# a create refused at each call, and its removals refused.
qiven3_add_cases(qiven3_unit_store_sqlite LABELS fast fake
  SOURCES store_sqlite_test.cpp store_sqlite_memory_test.cpp store_sqlite_states_test.cpp
          store_sqlite_scan_test.cpp store_sqlite_create_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_store)

# store/exchange's cases: the SQLite store's export, import and verify on
# the fake over a scratch directory; its smoke case: a round trip, a
# second import refused, an export from seqs and of a scope set, a refused
# import leaving nothing, an export closed by its store object.
qiven3_add_cases(qiven3_unit_store_exchange LABELS fast fake
  SOURCES store_exchange_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_store)
