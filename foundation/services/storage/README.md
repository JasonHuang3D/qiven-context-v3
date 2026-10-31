# storage

layer services, status group

Storage over bytes and keys: engine wrappers and stores.

- The services layer's edges: depends on core and system. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- No system header and no conditional on the system.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
