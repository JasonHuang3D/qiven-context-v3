# storage_sqlite

layer services, status built

- **Awaiting a split:** the record log's file identity and settings (`application_id`, `user_version` and the settings list in `src/store/engine/connection.hpp`) leave for `recordlog`, whose parameters they are
- **Holds:** the SQLite wrapper: `Connection` and `Statement` (open, exec, prepare, bind, step, reset, finalize, the busy wait, rollback, close), `Setting`, `EngineHeap` and its slots
- **Never holds:** a store's schema, SQL or views; a store, entry or scope; a store's settings or file identity but the record log's, until they leave
- **Contract:** `src/store/engine/connection.hpp`
- **Files:** not yet in this directory: its files are the `from` row's; its contract's header moves to `include/qiven/storage_sqlite/connection.hpp`.
- **Consumers rely on:** every engine allocation on a `memory` heap; settings written and read back
- **Parameters:** none
- **Refuses:** every failed engine call, as an error with the engine's extended code and the call's site; the engine's NOMEM as memory's budget error
