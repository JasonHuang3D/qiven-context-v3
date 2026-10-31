# test

layer devkit, status group

How any module is tested: the test kit, four modules of one target each, `runner`, `case_main`, `faults` and `scan`.

- The kit depends on the core, system and services modules its cases use (memory, log, diag, the platform's contract), never on a kit or a product: the one place an edge points up, since nothing but a test depends on it. Linked into tests alone, never into a module's library or a product's program.
- A module's fault seam (memory's failing countdown, the platform's failing calls) is that module's own code under `QIVEN_FAULTS`, never an edge to this group.

Each subdirectory holding a module.json is one module; its README.md is its charter.
