# test

layer devkit, status group

How any module is tested: the test kit, four modules of one target each, `runner`, `case_main`, `faults` and `scan`, and `harness`, the runner's tool half, no target: the CMake wiring, the listing, the watcher and the Python tests' binaries, meeting the kit by process alone.

- The kit depends on the core, system and services modules its cases use (memory, log, diag, the platform's contract; `scan` on none; `harness` on `python` alone, its tests running diag's fault program), never on a kit or a product: an edge up by design, since nothing but a test depends on it. It takes what those modules give as it stands, and each later step of the foundation's rebuild switches this edge's consumers in its own change. Linked into tests alone, never into a module's library or a product's program. Each row states link closure, `text` among them; each charter's "Uses directly" line names what its module takes.
- A module's fault seam (memory's failing countdown, the platform's failing calls) is that module's own code under `QIVEN_FAULTS`, never an edge to this group.

Each subdirectory holding a module.json is one module; its README.md is its charter.
