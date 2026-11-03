# sqlite

layer system, status built

- **Holds:** the vendored SQLite amalgamation, pinned, and its one build, the foundation's
- **Never holds:** a wrapper, a schema, any caller's SQL, or a default of a setting a caller sets on its connection
- **Contract:** `foundation/system/sqlite/include/qiven/sqlite/sqlite3.h`
- **Files:** `include/qiven/sqlite/` (`sqlite3.h`, vendored), `src/` (`sqlite3.c`, the amalgamation, and `README.md`, its vendoring's record: version, source, digests, the options' reasons, updating), `CMakeLists.txt` (the pin, the SHA-256 of both vendored files checked on every configure, and the one target `qiven_sqlite` with its options)
- **Consumers rely on:** the pinned engine built with the options of `src/README.md`: threading serialized, FTS5 in, the behaviour switches and the features left out
- **Parameters:** none
- **Refuses:** what its options omit: extension loading, the shared cache, the progress callback, declared types, implicit initialisation; a double-quoted string literal
