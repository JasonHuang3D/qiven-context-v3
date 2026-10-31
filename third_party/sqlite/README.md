# SQLite, vendored

`sqlite3.c` and `sqlite3.h` are SQLite 3.53.4's amalgamation as extracted,
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

Configure checks both files' SHA-256 (the top `CMakeLists.txt`) and fails
on any other, so a changed amalgamation never builds. The target
`qiven3_sqlite` compiles `sqlite3.c` as C at its own level: MSVC `/W3`
without `/WX`, gcc and clang their defaults without `-Werror`; the core's
warnings, analysis and frame bound do not cover it. `qiven3_store` alone
links it (B1).

## The options

The 21 options of `qiven3_sqlite`, each `SQLITE_`-prefixed:

| Option | Reason |
|---|---|
| `THREADSAFE=1` | serialized, the default; the store's threads each hold a connection |
| `DQS=0` | a double-quoted string literal is refused, a misfeature off; no store statement uses one |
| `DEFAULT_SYNCHRONOUS=2` | FULL, as the store sets on each connection |
| `DEFAULT_WAL_SYNCHRONOUS=2` | FULL in WAL mode too |
| `DEFAULT_PAGE_SIZE=4096` | the store's page size, pinned against a changed default |
| `DEFAULT_CACHE_SIZE=-2000` | the default, 2,000 KiB a connection, pinned |
| `DEFAULT_WAL_AUTOCHECKPOINT=1000` | the store's 1,000 pages, pinned |
| `DEFAULT_AUTOVACUUM=0` | `auto_vacuum` NONE, as the store sets it |
| `SECURE_DELETE` | `secure_delete` ON by default, as each connection sets it |
| `TEMP_STORE=2` | temporary storage in memory by default; the pragma still set |
| `DEFAULT_FOREIGN_KEYS=1` | `foreign_keys` ON by default, as each connection sets it |
| `TRUSTED_SCHEMA=0` | `trusted_schema` OFF by default, as each connection sets it |
| `ENABLE_FTS5` | the engine item's index; no core table uses it |
| `ENABLE_API_ARMOR` | a misuse of the API returns `SQLITE_MISUSE` rather than undefined behaviour |
| `LIKE_DOESNT_MATCH_BLOBS` | sqlite.org's recommendation; no store statement uses `LIKE` |
| `STRICT_SUBTYPE=1` | sqlite.org's recommendation; v3 defines no SQL function |
| `OMIT_AUTOINIT` | the core calls `sqlite3_initialize()` itself, before its first connection |
| `OMIT_SHARED_CACHE` | shared cache is never used; recommended, faster |
| `OMIT_LOAD_EXTENSION` | no extension is ever loaded (B10) |
| `OMIT_DECLTYPE` | the declared type of a column is never read |
| `OMIT_PROGRESS_CALLBACK` | the store's wait is the busy timeout, never a progress handler |

The four defaults that agree with the store's settings (`SECURE_DELETE`,
`TEMP_STORE=2`, `DEFAULT_FOREIGN_KEYS=1`, `TRUSTED_SCHEMA=0`) keep a
connection on them even if a pragma were missed; each connection still
sets and reads back every one. SQLite allocates through
`SQLITE_CONFIG_MALLOC`, routed to v3's heap, which needs no option.

## Updating

A new version replaces both files from sqlite.org's amalgamation zip, each
checked against the publisher's digest, and the pins in
`foundation/devkit/build/build.cmake` and this file in the same change.
