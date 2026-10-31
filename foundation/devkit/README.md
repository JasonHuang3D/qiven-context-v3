# devkit

layer devkit, status group

How any module is built and tested: the build's flags and presets, the test runner and faults kit, the
structure method. Linked into tests alone, never into a product's program.

- Its test kit depends on the core, system and services modules its cases use (memory, log, diag), never on a kit or a product: the one place an edge points up, since nothing but a test depends on it. `structure` depends on qiven3's modules today, until its product data leaves for `products/qiven3`. Within a layer a module depends on another only by a declared edge; the graph has no cycle.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
