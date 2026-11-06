# devkit

layer devkit, status group

How any module is built and tested: the build's flags and presets, the test runner and faults kit, the
structure method, and Python's lowest layer. The test kit is linked into tests alone, never into a product's program. The build's
options set for the whole tree (sanitizers, hardening, path maps, floors) reach every target, the vendored SQLite engine too;
`qiven_build`'s flags reach every target of ours, a product's program too, and never the engine (`qiven_sqlite`, compiled
at its own level).

- Its test kit depends on the core, system and services modules its cases use (memory, log, text, diag, the platform's contract), never on a kit or a product: an edge that points up, since nothing but a test depends on it. `structure` depends on qiven3's modules today, until its product data leaves for `products/qiven3`; `build` holds product data too (its README). Within a layer a module depends on another only by a declared edge; the graph has no cycle.
- It reads above `foundation/` by edges no row declares, the repository's data alone, from the checkout's root, `foundation/`'s parent (`qiven_root`, `build`'s `build.cmake`): the trees, `tools/trees.json` (`build`'s `top.cmake`, which names it for `harness`' `cases.cmake`, a dependency of its listing step, and `structure`'s `CMakeLists.txt`, one of its records step's; `build`'s `trees.cmake` and `tests/test_trees_build.py`; `python`'s `table.py`, through which `structure`'s `modules.py` and `harness`' `list_tests.py` read them); the table files, `tools/module.json` and the root's `module.json`, named by `tools/trees.json` alone; the root's `.clang-tidy` (`top.cmake`, under the analysis); and the old places' base, `tools/loop_base.json` (`structure`'s `modules.py`, its `BASE`), which the foundation's build alone leaves out with `structure`'s tests. It imports nothing of `tools/`, which `structure`'s import check holds, and the debugger's pin came into `python` (`debugger_pins.txt`, `1612fde`).

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
