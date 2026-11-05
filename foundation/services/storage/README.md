# storage

layer services, status group

Storage over bytes and keys: engine wrappers and stores.

- The services layer's edges: depends on core and system. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A dependency on what is not ours (an operating system, a third-party library, an SDK) is ours to own: one foundation module wraps it, with its headers private to that module or vendored; no foreign header reaches a header of ours; all other code calls only its own lower layers and the standard library (`DIRECTION-3.md`). A named departure in this group: SQLite's two handle types (`sqlite`'s `connection.hpp`), end by the owner's decision; the rest in `foundation/README.md`.
- No conditional on the system; a static size pin guarded by the compiler's library layout (`_MSC_VER`, `_WIN64`, the iterator debug level) is none: it checks a layout and changes no code. Counts measured on each system are chosen by one, in test code alone (`sqlite/tests/sqlite_test.cpp`, `recordlog/tests/store_sqlite_counts_test.cpp`, `recordlog/tests/store_exchange_counts_test.cpp`).

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
