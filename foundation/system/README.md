# system

layer system, status group

The operating system and devices, each behind a contract: one adapter per system and a fake, each passing
the same contract suite.

- Depends on devkit and core, but diag on log today, for log's ring, which moves into diag. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- The only layer whose sources include a system's or a device SDK's headers, beside the vendored sources, the tooling's git shim and qiven3's test programs (`tests/child/platform.cpp`, `tests/driver/common.cpp`).

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
