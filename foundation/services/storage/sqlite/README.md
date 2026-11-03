# storage_sqlite

layer services, status built

- **Holds:** the target `qiven_storage_sqlite`, the SQLite wrapper: `Connection` and `Statement` (open with its caller's busy wait, exec, prepare, bind, step, reset, finalize, rollback, close), `Setting` and `When`, `EngineHeap` and its slots, their sizes (`page_size`, `cache_slots`, `slots_reserve`, `default_connections`) defaults its caller may change (`Cache`); its typed error, `EngineError`, at the caller's `Site`; the engine calls' countdown under `QIVEN_FAULTS`; the one target that links the vendored engine, `qiven_sqlite`, and whose source alone includes the engine's header
- **Never holds:** a store's schema, SQL or views; a store, entry or scope; a store's settings or busy wait; a store's file identity; a caller's error, which the caller maps from `EngineError`
- **Contract:** `foundation/services/storage/sqlite/include/qiven/storage_sqlite/connection.hpp`
- **Files:** `include/qiven/storage_sqlite/` (`connection.hpp`), `src/` (`sqlite.cpp`, with its own analysis configuration), `CMakeLists.txt`; their namespace `qiven::storage_sqlite`
- **Consumers rely on:** every engine allocation on a `memory` heap; settings written and read back; every failed call typed
- **Parameters:** none
- **Refuses:** every failed engine call, `EngineError` with the engine's extended code and the call's site; the engine's NOMEM as memory's budget error, naming `storage_sqlite`; its own refusals, `EngineError` with a refusal of its own: no engine heap, a statement past the engine's length, an empty statement, a query with no row, an engine heap installed already, pages that cannot hold the slots, the engine refusing its configuration
- **Words:**
  - `engine heap`: the process's one `memory` heap the engine allocates on, its page cache on slots at the back of its pages
  - `site`: the operation and the path a caller names a call by
