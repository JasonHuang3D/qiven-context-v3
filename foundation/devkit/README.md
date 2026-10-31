# devkit

layer devkit, status group

How any module is built and tested: the build's flags and presets, the test runner and faults kit, the
structure method. Linked into tests alone, never into a product's program.

- Depends on nothing above it. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
