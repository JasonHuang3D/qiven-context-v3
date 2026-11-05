# services

layer services, status group

General services over core and system, portable, reaching the system through system's contracts alone.

- Depends on core and system. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A dependency on what is not ours (an operating system, a third-party library, an SDK) is ours to own: one foundation module wraps it, with its headers private to that module or vendored; no foreign header reaches a header of ours; all other code calls only its own lower layers and the standard library (`DIRECTION-3.md`). A named departure in this layer: SQLite's two handle types (`storage/sqlite`'s `connection.hpp`), end at the order's step (d), where they become opaque in SQLite's one owner in `system/` (`foundation/README.md`); the rest in `foundation/README.md`.
- No conditional on the system; a static size pin guarded by the compiler's library layout (`_MSC_VER`, `_WIN64`, the iterator debug level) is none: it checks a layout and changes no code. A test's counts measured on each system (`storage/`'s, in its README) are chosen by one, in test code alone.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
