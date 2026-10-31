# recordlog

layer services, status built

- **Holds:** the record log: the store contract over bytes and keys, its SQLite and in-memory stores, export and import, the keys, and the stores' test books and seams
- **Never holds:** a product's rules over a record (a product's admit hook holds them)
- **Contract:** `src/store/contract.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's headers move to `include/qiven/recordlog/contract.hpp`, `include/qiven/recordlog/keys.hpp`.
- **Consumers rely on:** a scoped, chained, append-only log; each write through the caller's admit hook; typed `StoreError` kinds; one contract met by every store
- **Parameters:** `limits`; `application_id` (not built yet); `user_version` (not built yet); `id_bytes` (not built yet); `scope_bytes` (not built yet); `lane_bytes` (not built yet)
- **Refuses:** a key past its size; a write past `limits`; a file of another identity; a head that is not the expected one; a busy or locked file
- **Words:**
  - `record`: a line of keys (id, scope, lane, kind, seq, parent, target) and its body
  - `scope`: the name records are selected by; a set's entry ending `*` holds every scope it begins
  - `lane`: a chain of records within a scope, its head the last record's id and seq
  - `forget`: the erasure of a record's body, its keys kept
  - `engine`: a store's storage below the contract: SQLite or memory; other engines are adapter slots of this module
