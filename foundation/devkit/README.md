# devkit

layer devkit, status group

How any module is built and tested: the build's flags and presets, the test runner and faults kit, the
structure method. The test kit is linked into tests alone, never into a product's program; the build's `qiven_build`
and `qiven_executable` reach every target, a product's program too.

- Its test kit depends on the core, system and services modules its cases use (memory, log, text, diag, the platform's contract), never on a kit or a product: an edge that points up, since nothing but a test depends on it. `qiven_executable` names diag's entry targets, an edge `build`'s row does not declare. `structure` depends on qiven3's modules today, until its product data leaves for `products/qiven3`; `build` holds product data too (its README). Within a layer a module depends on another only by a declared edge; the graph has no cycle.
- It reads `tools/` by edges no row declares: the runner (`binaries.py`, `list_tests.py`, `cases.cmake`, `ctest_tests.cmake`, `tests/test_wiring.py`: the status table, the table's reader, the watcher, the counting shim); `structure` (`layers.py`, `records.py`, `CMakeLists.txt`, `tests/test_floors.py`, `tests/test_line_endings.py`: the table's reader and git's door); `build` (`trees.cmake`, `tests/test_trees_build.py`, `tests/test_hardened.py`: the trees, the table's reader, the status table). diag's `tests/test_faults.py` reads the status table too. Whether they move is the owner's decision.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
