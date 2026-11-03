# SQLite, vendored

`sqlite3.c` (here, `src/`) and `sqlite3.h` (`include/qiven/sqlite/`, the
module's contract) are SQLite 3.53.4's amalgamation as extracted,
unchanged. `shell.c` and `sqlite3ext.h` are not kept. SQLite is in the
public domain.

| Item | Value |
|---|---|
| version | 3.53.4, released 2026-07-24; `SQLITE_VERSION_NUMBER` 3053004 |
| `SQLITE_SOURCE_ID` | `2026-07-24 19:02:57 bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` |
| source | `https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip` |
| the publisher's digest | SHA3-256 of `sqlite3.c`, from the 3.53.4 release log: `67f423e9ebbbdc473cbc4772c872ee6b89f31fde4ed0279a5c25d5f65c043a16` |
| the zip | 2,946,650 bytes, SHA-256 `1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d` |
| `sqlite3.c` | 9,515,341 bytes, SHA-256 `b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189` |
| `sqlite3.h` | 690,838 bytes, SHA-256 `919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d` |

Configure checks both files' SHA-256 (the module's `CMakeLists.txt`, at its
top) and fails on any other, so a changed amalgamation never builds. The
target `qiven_sqlite` (the same file) compiles `sqlite3.c` as C at its own level: MSVC `/W3`
without `/WX`, gcc and clang their defaults without `-Werror`; the core's
warnings, analysis and frame bound do not cover it. `qiven_storage_sqlite`
alone links it (B1).

## The options

The foundation's one build of the engine: the 11 options of
`qiven_sqlite`, each `SQLITE_`-prefixed, each for a reason that holds for
any caller of this build:

| Option | Reason |
|---|---|
| `THREADSAFE=1` | threading: serialized, SQLite's default, kept explicit; a caller may use its connections from any thread without corrupting the engine |
| `DQS=0` | a behaviour switch: a double-quoted string literal is refused in statements and in schemas, so a misspelt column name is an error, never a string; the amalgamation's own advice ("`-DSQLITE_DQS=0` (best)"). It is the default of a connection's `SQLITE_DBCONFIG_DQS_DML` and `SQLITE_DBCONFIG_DQS_DDL`, which no pragma sets: kept until a caller's connection sets them by `sqlite3_db_config` |
| `ENABLE_FTS5` | a feature in: the full-text index, which no caller can add to a build without it; shown by the case `store_sqlite.fts5` |
| `ENABLE_API_ARMOR` | a behaviour switch: a misuse of the API returns `SQLITE_MISUSE` rather than undefined behaviour |
| `LIKE_DOESNT_MATCH_BLOBS` | a behaviour switch: `LIKE` and `GLOB` never match a BLOB, so a pattern compares text alone; sqlite.org's recommendation |
| `STRICT_SUBTYPE=1` | a behaviour switch: an SQL function that sets a result's subtype without declaring `SQLITE_RESULT_SUBTYPE` raises an error, rather than its subtype being lost in some cases (the amalgamation's comment on `sqlite3_result_subtype`); sqlite.org's recommendation |
| `OMIT_AUTOINIT` | a feature out: the engine never starts by itself, so it starts only once its caller has configured it (its allocator) and called `sqlite3_initialize()` |
| `OMIT_SHARED_CACHE` | a feature out: no connection shares another's cache; sqlite.org's recommendation, and faster |
| `OMIT_LOAD_EXTENSION` | a feature out: no extension can be loaded, so no code outside this pinned build runs in the engine |
| `OMIT_DECLTYPE` | a feature out: a statement keeps no declared types of its columns; sqlite.org's recommendation |
| `OMIT_PROGRESS_CALLBACK` | a feature out: no progress handler, its check gone from every statement's loop; a caller's wait is the busy timeout; sqlite.org's recommendation |

The build sets no default of a setting a caller sets by a pragma on its
connection (the page size, the page cache's size, the WAL's automatic
checkpoint, `synchronous`, `auto_vacuum`, `secure_delete`, `temp_store`,
`foreign_keys`, `trusted_schema`): SQLite's own defaults hold until a
caller sets them, and each caller sets and reads back its own. SQLite
allocates through `SQLITE_CONFIG_MALLOC`, routed by its caller to its own
heap, which needs no option.

## Updating

A new version replaces both files from sqlite.org's amalgamation zip, each
checked against the publisher's digest, and the pins in the module's
`CMakeLists.txt` and this file in the same change.
