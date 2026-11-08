# recordlog

layer services, status built

- **Holds:** the record log: the target `qiven_recordlog`, the store contract over bytes and keys, its typed errors, stage and the store's own guards over the keys, its SQLite and in-memory stores, export and import; the SQLite store over `storage_sqlite`'s wrapper, through its own `Connection`, `Statement` and `EngineHeap` (`sqlite_connection.hpp`), which map the wrapper's `EngineError` to `StoreError` in one place (`sqlite_connection.cpp`); the target `qiven_recordlog_keys`, the keys alone (`keys.hpp`), a target that names the keys without the store
- **Never holds:** a product's rules over a record (a product's admit hook holds them); a call into the engine, which is `storage_sqlite`'s
- **Contract:** `foundation/services/storage/recordlog/include/qiven/recordlog/contract.hpp`
- **Files:** `include/qiven/recordlog/` (`contract.hpp`, `keys.hpp`, `values.hpp`, `errors.hpp`, `codes.hpp`, `stage.hpp`, `memory.hpp`, `memory_state.hpp`, `memory_exchange.hpp`, `sqlite_store.hpp`, `sqlite_store_impl.hpp`, `sqlite_create.hpp`, `sqlite_steps.hpp`, `sqlite_connection.hpp`, `exchange.hpp`), `src/` (`errors.cpp`, `stage.cpp`, `memory_state.cpp`, `memory_exchange.cpp`, `sqlite_connection.cpp`, `sqlite_open.cpp`, `sqlite_read.cpp`, `sqlite_write.cpp`, `exchange.cpp`; `sizes/`: `sizes.cpp`, with its own analysis configuration), `tests/` (the store's cases over its own plain records, `CMakeLists.txt`, the targets `qiven_recordlog_*_test`; `support/stores/`, the target `qiven_recordlog_stores`; `support/seams/`, the target `qiven_recordlog_seams`), `CMakeLists.txt`; their namespace `qiven::recordlog`, the stores' state `qiven::recordlog::state`, stage `qiven::recordlog::stage`, the SQLite store's open, reads, writes and exchange, and its connection over the wrapper's, `qiven::recordlog::sqlite`
- **Consumers rely on:** a scoped, chained, append-only log; each write through the caller's admit hook; typed `StoreError` kinds; one contract met by every store
- **Parameters:** `limits`; `application_id`; `id_bytes` (not built yet); `scope_bytes` (not built yet); `lane_bytes` (not built yet)
- **Refuses:** a key past its size; a write past `limits`; a file of another identity; a head that is not the expected one; a busy or locked file
- **Words:**
  - `record`: a line of keys (id, scope, lane, kind, seq, parent, target) and its body
  - `scope`: the name records are selected by; a set's entry ending `*` holds every scope it begins
  - `lane`: a chain of records within a scope, its head the last record's id and seq
  - `forget`: the erasure of a record's body, its keys kept
  - `engine`: a store's storage below the contract: SQLite or memory; other engines are adapter slots of this module
