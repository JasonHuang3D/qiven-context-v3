# The record log's own tests, over its own plain records, and their
# support under support/, whose targets give this directory as their
# include root, so that a caller's "support/stores/..." include holds.
# Added by tests/CMakeLists.txt, where qiven_add_cases, qiven_faults and
# qiven_case_main are made.

# qiven_recordlog_stores: the factory the store cases take and their tags,
# a header binding the in-memory store and the SQLite store on the fake
# platform, each with no admit hook, and the plain records the cases write.
add_library(qiven_recordlog_stores INTERFACE)
target_include_directories(qiven_recordlog_stores INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(qiven_recordlog_stores INTERFACE qiven_recordlog qiven_platform_fake qiven_runner)

# qiven_recordlog_seams: the store's seams, hooks types and calls through
# its connection, and the naive file store, M5's and M6's negative control.
add_library(qiven_recordlog_seams STATIC
  support/seams/seams.cpp
  support/seams/seams.hpp
)
target_include_directories(qiven_recordlog_seams PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(qiven_recordlog_seams PUBLIC qiven_recordlog PRIVATE qiven_build)

# errors' cases: each kind with its fields and its code, a hook's code
# with no id, the cuts at a code point and the names, evaluated by the
# compiler; each kind's line; an error thrown and caught; the fault run
# over none.
qiven_add_cases(qiven_recordlog_errors_test LABELS fast fake
  SOURCES store_errors_test.cpp
  LINK qiven_faults qiven_recordlog)

# codes' cases: the table's named numbers, its order and its names;
# each row of the mapping and each purge reason, evaluated by the compiler;
# every code at every place and step; a line; the fault run over none.
qiven_add_cases(qiven_recordlog_codes_test LABELS fast fake
  SOURCES store_codes_test.cpp
  LINK qiven_faults qiven_recordlog)

# contract's cases: a minimal store, scan and export the concepts
# accept and a plant of each that they refuse; the names, read-only's
# operations, the kinds each raises, the capabilities' applicability, the
# limits, the identity and volume and the budgets, evaluated by the compiler; the minimal store
# driven through the concept; the gate's refusals in order; a line on the
# caller's arena; the fault run over none.
qiven_add_cases(qiven_recordlog_contract_test LABELS fast fake
  SOURCES store_contract_test.cpp
  LINK qiven_faults qiven_recordlog)

# store's view cases: the generic calls, a line by id, a record's targets,
# a scope's ids of a kind with no target and a byte search over the lines
# held, on the in-memory store's view and the SQLite store's, on the fake.
qiven_add_cases(qiven_recordlog_view_test LABELS fast fake
  SOURCES store_view_test.cpp
  LINK qiven_faults qiven_recordlog_stores)

# store's generic calls on the store itself, outside any write, over plain
# records on the plain factory: a line by id, an id's targets in a scope,
# a scope's ids of a kind with no target and their lent walk; the order,
# the scopes kept apart, absent ids, a key past its bound, the budget, a
# closed object; written as templates on a factory, in both stores, the
# SQLite one reopened before the reads.
qiven_add_cases(qiven_recordlog_calls_test LABELS fast fake
  SOURCES store_calls_test.cpp
  LINK qiven_faults qiven_recordlog_stores)

# store's own guards in both stores with no admit hook, on the fake: a
# forget appended, a forget given a record not a forget, a forget's target
# not erasable, an import's seq gap and wrong parent, the order on a lane with
# a head and an entry of another scope or lane; each refusal followed by
# verify and, on SQLite, a reopen, the state as before.
qiven_add_cases(qiven_recordlog_guard_test LABELS fast fake
  SOURCES store_guard_test.cpp
  LINK qiven_faults qiven_recordlog_stores)

# store's plain path in both stores with no admit hook, on the fake: the body
# key held and given alike, each write followed by verify and, on SQLite, a
# reopen and verify again; a body row lost or stray refused.
qiven_add_cases(qiven_recordlog_plain_test LABELS fast fake
  SOURCES store_plain_test.cpp
  LINK qiven_faults qiven_recordlog_stores)

# store's plain path at a large import, on SQLite and the fake: 20,000 lines,
# body keys with no record. Not of the label fast, so the limit of any other
# test, since it takes more than the fast one's under AddressSanitizer.
qiven_add_cases(qiven_recordlog_plain_many_test LABELS fake
  SOURCES store_plain_many_test.cpp
  LINK qiven_faults qiven_recordlog_stores)

# memory's cases: the in-memory store over one state on the fake;
# its smoke and exchange cases, its budgets, counts and fault runs, its
# lock, thread gate, read-only object and forged states; a scan's end at
# each lane's length at its start (scan_case.hpp); all over plain records
# with no admit hook.
qiven_add_cases(qiven_recordlog_memory_test LABELS fast fake
  SOURCES store_memory_test.cpp store_memory_memory_test.cpp store_memory_threads_test.cpp
          store_memory_scan_test.cpp
  LINK qiven_faults qiven_platform_fake qiven_recordlog)
# The stores' support by its path under this directory, which the fake
# gave before its move.
target_include_directories(qiven_recordlog_memory_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# sqlite_store's cases: the SQLite store on the fake over a scratch
# directory; its smoke case: create, identify, the settings read back, the
# files open refuses, the guard and coherence; its writes case: append,
# forget, purge, size and close; its reads; its budgets, counts and fault
# runs, the create's and the forget's in parts; a purge held pending,
# forged files, an engine refused and its refusals as StoreError; a scan's
# end at each lane's length at its start (scan_case.hpp), and entry's
# rowids kept by a purge's VACUUM between two steps of a scan, read through
# the seams; a create refused at each call, in parts, and its removals
# refused.
qiven_add_cases(qiven_recordlog_sqlite_test LABELS fast fake
  SOURCES store_sqlite_test.cpp store_sqlite_memory_test.cpp store_sqlite_states_test.cpp
          store_sqlite_counts_test.cpp
          store_sqlite_scan_test.cpp store_sqlite_create_test.cpp store_sqlite_create_parts_test.cpp
          store_sqlite_parts_test.cpp
  LINK qiven_faults qiven_platform_fake qiven_recordlog_seams qiven_recordlog)

# exchange's cases: the SQLite store's export, import and verify on
# the fake over a scratch directory; its smoke case: a round trip, a
# second import refused, an export from seqs and of a scope set, a refused
# import leaving nothing, an export closed by its store object; an
# export's reader refusing limits and an identity as open does; the
# budgets, counts and fault runs of export, heads, next, import and verify;
# a maximum entry and a large lane; StoreBusy and StoreNotEmpty at import,
# the thread gate, the exports' closing, import's rollback and checkpoint;
# all over plain records with no admit hook.
qiven_add_cases(qiven_recordlog_exchange_test LABELS fast fake
  SOURCES store_exchange_test.cpp store_exchange_memory_test.cpp store_exchange_counts_test.cpp store_exchange_states_test.cpp
  LINK qiven_faults qiven_platform_fake qiven_recordlog)
# The stores' support by its path under this directory, which the fake
# gave before its move.
target_include_directories(qiven_recordlog_exchange_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# The store cases' support, over plain records on the plain factory: the
# factory over each store and its tags, its objects given to the case
# template of a scan's end; the seams, the steps reported, an incoherent row
# found at open, VACUUM and a checkpoint, the naive file store; a SQLite
# store's directory scanned by the raw-byte scans.
qiven_add_cases(qiven_recordlog_stores_test LABELS fast fake
  SOURCES stores_test.cpp seams_test.cpp scan_test.cpp
  LINK qiven_recordlog_stores qiven_recordlog_seams qiven_scan qiven_runner)
