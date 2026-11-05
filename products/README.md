# products

layer products, status group

Each product's own concepts and rules. A product chooses its adapters in its program alone.

- Depends on the foundation and the kits; never on another product. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A dependency on what is not ours (an operating system, a third-party library, an SDK) is wrapped by one foundation module; a product calls only its own lower layers and the standard library (`DIRECTION-3.md`). So it holds no system header and no conditional on the system, its program and its tests' programs included: that follows from the rule, and where a file stands is never its test. What does not meet it today, each a named departure with its end, at the step that builds the member it needs, its consumer switching in that change:
  - `src/app/instances.cpp`'s four conditionals (the entry, `wmain`, `make_platform`, `location::main<Current>`): end at the order's step (d) (`foundation/README.md`), where platform gives a generic entry with UTF-8 arguments below the product, and its `Current` is chosen inside platform.
  - qiven3's native path syntax (`src/entry/projectmap_files.hpp`'s `NATIVE`): end at step (d), by a platform member.
  - `tests/child/`'s conditionals, `tests/driver/common.cpp`'s, and the system's header `tests/child/init.cpp` and `tests/driver/common.cpp` include: end at P4 (`foundation/README.md`).

  None of these, by the allowance `foundation/services/README.md` states: a test's counts measured on each system and chosen by one, in test code alone (`tests/unit/location_memory_test.cpp`); a case stating where it applies (`tests/unit/location_cases_test.cpp`), as the Python's `system.WINDOWS` skips. Nor is a static size pin guarded by the compiler's library layout (`src/entry/header.hpp`, `tests/unit/body_test.cpp`, `check_test.cpp`): it checks a layout and changes no code.
- Code names a contract, never an adapter, but in the product's program, which chooses its adapters.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
