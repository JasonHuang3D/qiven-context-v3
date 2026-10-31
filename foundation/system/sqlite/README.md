# sqlite

layer system, status built

- **Holds:** the vendored SQLite amalgamation, pinned, and its build
- **Never holds:** a wrapper, a schema, or any caller's SQL
- **Contract:** `third_party/sqlite/sqlite3.h`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's header, the vendored one, moves to `include/qiven/sqlite/sqlite3.h`.
- **Consumers rely on:** the pinned engine built with `compile_options`
- **Parameters:** `compile_options` (not built yet)
- **Refuses:** what its options omit: extension loading, the shared cache, the progress callback, declared types
