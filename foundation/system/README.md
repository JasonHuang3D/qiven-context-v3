# system

layer system, status group

The operating system and devices, each behind a contract: one adapter per system and a fake, each passing
the same contract suite.

- Depends on devkit and core. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- The only layer whose sources include a system's or a device SDK's headers.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
