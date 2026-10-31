# sqlite

layer system, status built

- **Holds:** the vendored SQLite amalgamation, pinned, and its build
- **Never holds:** a wrapper, a schema, or any caller's SQL
- **Contract:** `include/qiven/sqlite/sqlite3.h`, the vendored header; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** store
- **Consumers rely on:** the pinned engine built with `compile_options`
- **Consumers give:** `compile_options`
- **Refuses:** what its options omit: extension loading, the shared cache, the progress callback, declared types
