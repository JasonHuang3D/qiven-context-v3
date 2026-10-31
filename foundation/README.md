# foundation

layer foundation, status group

Layers devkit, core, system and services: the code any kit or product builds on.
A module here names no product's concept and depends on no kit and no product, but for the parts a module's
README names as awaiting a split, and devkit's `structure`, whose edges to qiven3's modules leave with its
product data.

- An edge goes down only, but devkit's test kit's edges to the modules its cases use; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A system's or a device SDK's header only in `system/` and in vendored sources.
- A contract is its module's public headers, stated as concepts; they move to `include/qiven/<module>/` with the module's files, and until then each built module's `contract` field names its entry header where it stands.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
