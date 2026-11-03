# storage_sqlite

layer services, status built

- **Awaiting a split:** its files, still `recordlog`'s (below), move here; the record log's settings have left them for `recordlog`'s `include/qiven/recordlog/sqlite_steps.hpp`, whose busy wait, open's name and countdowns `src/engine/sqlite.cpp` still reads, its errors still `recordlog`'s `StoreError` through its codes
- **Holds:** the SQLite wrapper: `Connection` and `Statement` (open, exec, prepare, bind, step, reset, finalize, the busy wait, rollback, close), `Setting`, `EngineHeap` and its slots
- **Never holds:** a store's schema, SQL or views; a store, entry or scope; a store's settings; a store's file identity
- **Contract:** `foundation/services/storage/recordlog/include/qiven/recordlog/engine/connection.hpp`
- **Files:** not yet in this directory: its files are the `from` row's, `recordlog`'s `include/qiven/recordlog/engine/` (`connection.hpp`) and `src/engine/` (`sqlite.cpp`); its contract's header moves to `include/qiven/storage_sqlite/connection.hpp`.
- **Consumers rely on:** every engine allocation on a `memory` heap; settings written and read back
- **Parameters:** none
- **Refuses:** every failed engine call, as an error with the engine's extended code and the call's site; the engine's NOMEM as memory's budget error
