# sqlite

layer system, status built

- **Holds:** the vendored SQLite amalgamation, pinned, and its build
- **Never holds:** a wrapper, a schema, or any caller's SQL
- **Contract:** `foundation/system/sqlite/include/qiven/sqlite/sqlite3.h`
- **Files:** `include/qiven/sqlite/` (`sqlite3.h`, vendored), `src/` (`sqlite3.c`, the amalgamation, and `README.md`, its vendoring's record: version, source, digests, the options' reasons, updating), `CMakeLists.txt`, the one target `qiven_sqlite`
- **Consumers rely on:** the pinned engine built with `compile_options`
- **Parameters:** `compile_options` (not built yet)
- **Refuses:** what its options omit: extension loading, the shared cache, the progress callback, declared types
