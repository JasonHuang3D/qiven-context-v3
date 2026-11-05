# devkit

layer devkit, status group

How any module is built and tested: the build's flags and presets, the test runner and faults kit, the
structure method, and Python's lowest layer. The test kit is linked into tests alone, never into a product's program. The build's
options set for the whole tree (sanitizers, hardening, path maps, floors) reach every target, the vendored SQLite engine too;
`qiven_build`'s flags reach every target of ours, a product's program too, and never the engine (`qiven_sqlite`, compiled
at its own level); `qiven_executable` makes every program.

- Its test kit depends on the core, system and services modules its cases use (memory, log, text, diag, the platform's contract), never on a kit or a product: an edge that points up, since nothing but a test depends on it. `qiven_executable` names diag's entry targets, an edge `build`'s row does not declare. `structure` depends on qiven3's modules today, until its product data leaves for `products/qiven3`; `build` holds product data too (its README). Within a layer a module depends on another only by a declared edge; the graph has no cycle.
- It reads `tools/` by edges no row declares: the runner (`binaries.py`, `list_tests.py`, `cases.cmake`, `ctest_tests.cmake`, `tests/test_wiring.py`: the table's reader, the watcher); `structure` (`layers.py`, `records.py`, `CMakeLists.txt`, `tests/test_floors.py`: the table's reader); `build` (`trees.cmake`, `tests/test_trees_build.py`: the trees, the table's reader). They move, at the order's step (a) (`foundation/README.md`): what they read comes into devkit, so that devkit reads no `tools/`.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
