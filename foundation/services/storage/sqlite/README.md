# storage_sqlite

layer services, status built

- **Holds:** the SQLite wrapper: `Connection` and `Statement` (open, exec, prepare, bind, step, reset, finalize, the busy wait, rollback, close), `Setting`, `EngineHeap` and its slots
- **Never holds:** a store's schema, SQL, views, settings or file identity; a store, entry or scope
- **Contract:** `include/qiven/storage_sqlite/connection.hpp`; not yet in this directory: its files are the `from` row's.
- **Consumers:** the `store` row
- **Consumers rely on:** every engine allocation on a `memory` heap; settings written and read back
- **Consumers give:** none
- **Refuses:** every failed engine call, as an error with the engine's extended code and the call's site; the engine's NOMEM as memory's budget error
