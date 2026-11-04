# The SQLite wrapper's own tests, over databases and an engine heap's
# pages of the case's own. Added by tests/CMakeLists.txt, where
# qiven_add_cases and qiven_case_main are made.

# sqlite's cases: the engine heap's page cache on its slots, a connection's
# and two connections' at once, within the slots and past them; the
# engine's full-text build; the engine heap's refusals, each EngineError
# with its refusal, code and detail: installed already, pages short of its
# slots and, under QIVEN_FAULTS, its configuration's failed call; calls
# refused at their caller's site, the site held and cut at a code point.
qiven_add_cases(qiven_storage_sqlite_test LABELS fast fake
  SOURCES sqlite_test.cpp
  LINK qiven_storage_sqlite)
