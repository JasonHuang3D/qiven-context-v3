# system

layer system, status group

The operating system and devices, each behind a contract: one adapter per system and a fake, each passing
the same contract suite.

- Depends on devkit and core. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A dependency on what is not ours (an operating system, a third-party library, an SDK) is ours to own: one foundation module wraps it, with its headers private to that module or vendored; no foreign header reaches a header of ours; all other code calls only its own lower layers and the standard library (`DIRECTION-3.md`). A named departure in this layer: SQLite's public engine header (in the `sqlite` module's `include/`), end at the order's step (d), where it goes private to SQLite's one owner in this layer (`foundation/README.md`); the rest in `foundation/README.md`. A test's counts measured on each system and chosen by one, in test code alone, and a case stating where it applies, are no departure.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
