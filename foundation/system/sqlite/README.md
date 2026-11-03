# sqlite

layer system, status built

- **Awaiting its split:** the store's half is held here: the compile options' values with their reasons (the definitions in `CMakeLists.txt`, `compile_options`' default in `module.json`, the table in `src/README.md`), chosen as the store sets its connections, and the B1 comments naming `qiven_recordlog` (`CMakeLists.txt`, `src/README.md`); they leave to the store when `compile_options` is built, the split waiting on the owner's choice
- **Holds:** the vendored SQLite amalgamation, pinned, and its build
- **Never holds:** a wrapper, a schema, or any caller's SQL
- **Contract:** `foundation/system/sqlite/include/qiven/sqlite/sqlite3.h`
- **Files:** `include/qiven/sqlite/` (`sqlite3.h`, vendored), `src/` (`sqlite3.c`, the amalgamation, and `README.md`, its vendoring's record: version, source, digests, the options' reasons, updating), `CMakeLists.txt` (the pin, the SHA-256 of both vendored files checked on every configure, and the one target `qiven_sqlite`)
- **Consumers rely on:** the pinned engine built with `compile_options`
- **Parameters:** `compile_options` (not built yet)
- **Refuses:** what its options omit: extension loading, the shared cache, the progress callback, declared types
