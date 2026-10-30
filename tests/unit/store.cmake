# store/errors' cases: each kind with its fields and its code, `secret`
# with no id, the cuts at a code point and the names, evaluated by the
# compiler; the widths against the entry's; each kind's line; an error
# thrown and caught; the fault run over none.
qiven3_add_cases(qiven3_unit_store_errors LABELS fast fake
  SOURCES store_errors_test.cpp
  LINK qiven3_faults qiven3_store qiven3_secret qiven3_entry)

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

# store's view cases: the generic calls, a line by id, a record's targets,
# a scope's ids of a kind with no target and a byte search over the lines
# held, on the in-memory store's view and the SQLite store's, on the fake.
qiven3_add_cases(qiven3_unit_store_view LABELS fast fake
  SOURCES store_view_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_store)

# store/memory's cases: the in-memory store over one state on the fake;
# its smoke and exchange cases, its budgets, counts and fault runs, its
# lock, thread gate, read-only object and checked reads; a scan's end at
# each lane's length at its start (store_scan_case.hpp).
qiven3_add_cases(qiven3_unit_store_memory LABELS fast fake
  SOURCES store_memory_test.cpp store_memory_memory_test.cpp store_memory_threads_test.cpp
          store_memory_scan_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_checked qiven3_store)

# store/sqlite's cases: the SQLite store on the fake over a scratch
# directory; its smoke case: create, identify, the settings read back, the
# files open refuses, the guard and coherence; its writes case: append,
# forget, purge, size and close; its reads; its budgets, counts and fault
# runs, the create's and the forget's in parts; the engine heap's page
# cache, a purge held pending, forged files; a scan's end at each lane's
# length at its start (store_scan_case.hpp), and entry's rowids kept by a
# purge's VACUUM between two steps of a scan, read through the seams; a
# create refused at each call, in parts, and its removals refused.
qiven3_add_cases(qiven3_unit_store_sqlite LABELS fast fake
  SOURCES store_sqlite_test.cpp store_sqlite_memory_test.cpp store_sqlite_states_test.cpp
          store_sqlite_scan_test.cpp store_sqlite_create_test.cpp store_sqlite_create_parts_test.cpp
          store_sqlite_parts_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_seams qiven3_checked qiven3_store)

# store/exchange's cases: the SQLite store's export, import and verify on
# the fake over a scratch directory; its smoke case: a round trip, a
# second import refused, an export from seqs and of a scope set, a refused
# import leaving nothing, an export closed by its store object; the
# budgets, counts and fault runs of export, heads, next, import and verify;
# a maximum entry and a large lane; StoreBusy and StoreNotEmpty at import,
# the thread gate, the exports' closing, import's rollback and checkpoint.
qiven3_add_cases(qiven3_unit_store_exchange LABELS fast fake
  SOURCES store_exchange_test.cpp store_exchange_memory_test.cpp store_exchange_states_test.cpp
  LINK qiven3_faults qiven3_platform_fake qiven3_fixtures qiven3_checked qiven3_store)

# The store cases' support: the factory over each store and its tags, its
# objects given to the case template of a scan's end; the seams, the steps
# reported, an incoherent row found at open, VACUUM and a checkpoint, the
# naive file store; the raw-byte scans' patterns and a directory scanned,
# a SQLite store's among them.
qiven3_add_cases(qiven3_unit_stores LABELS fast fake
  SOURCES stores_test.cpp seams_test.cpp scan_test.cpp
  LINK qiven3_stores qiven3_seams qiven3_scan qiven3_runner)
