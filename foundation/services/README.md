# services

layer services, status group

General services over core and system, portable, reaching the system through system's contracts alone.

- Depends on core and system. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- No system header and no conditional on the system.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
