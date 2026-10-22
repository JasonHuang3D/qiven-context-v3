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
# size, every fixture entry; the compiles counted, under the budget and the
# fault run.
qiven3_add_cases(qiven3_unit_content LABELS fast fake
  SOURCES content_test.cpp content_memory_test.cpp
  LINK qiven3_faults qiven3_fixtures qiven3_entry)

# check's cases: part 1's order, the parser's, the header's, the record's,
# its binding and the content's, a header with no body meeting basis and
# scope alone, evaluated by the compiler; every fixture entry with its
# record and with none, the refusals of A, the parser's on the line and the
# record, a content past 65,536 bytes; its budgets and fault runs.
qiven3_add_cases(qiven3_unit_check LABELS fast fake
  SOURCES check_test.cpp check_memory_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_entry)

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
