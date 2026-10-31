# foundation

layer foundation, status group

Layers devkit, core, system and services: the code any kit or product builds on.
A module here names no product's concept and depends on no kit and no product.

- An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A system's or a device SDK's header only in `system/` and in vendored sources.
- A contract is its module's public headers under `include/qiven/<module>/`, stated as concepts.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
