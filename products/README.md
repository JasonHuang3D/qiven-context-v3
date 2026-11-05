# products

layer products, status group

Each product's own concepts and rules. A product chooses its adapters in its program alone.

- Depends on the foundation and the kits; never on another product. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A dependency on what is not ours (an operating system, a third-party library, an SDK) is wrapped by one foundation module; a product calls only its own lower layers and the standard library (`DIRECTION-3.md`). So it holds no system header and no conditional on the system, its program and its tests' programs included: that follows from the rule, and where a file stands is never its test. What does not meet it today, each a named departure with its end:
  - qiven3's entry point, `wmain` or `main` by the system (`src/app/instances.cpp`): end at the order's step (d) (`foundation/README.md`), where platform gives a generic entry with UTF-8 arguments below the product.
  - qiven3's native system of a path (`src/entry/projectmap_files.hpp`): end at P4.
  - `tests/child/`'s conditionals, and the system's header `tests/child/init.cpp` and `tests/driver/common.cpp` include: end at P4 (`foundation/README.md`).
- Code names a contract, never an adapter, but in the product's program, which chooses its adapters.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
