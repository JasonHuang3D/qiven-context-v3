# foundation

layer foundation, status group

Layers devkit, core, system and services: the code any kit or product builds on.
A module here names no product's concept and depends on no kit and no product, but for the parts a module's
README names as awaiting a split, and devkit's `structure`, whose edges to qiven3's modules leave with its
product data.
The foundation cannot build alone yet: `foundation/CMakeLists.txt` does not exist, the root `CMakeLists.txt` adds the
product's directories, and devkit and diag's tests read `tools/` (`devkit/README.md`). The foundation is rebuilt lowest
first, in the owner's order: (a) its build alone, with the table's reader and the watcher in devkit and Python's lowest
layer; (b) devkit; (c) core; (d) system; (e) services; the product's at P4. That build ends step (a).

- An edge goes down only, but devkit's test kit's edges to the modules its cases use, and `qiven_executable`'s, in `build`, to diag's entry targets; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A dependency on what is not ours (an operating system, a third-party library, an SDK) is ours to own: one foundation module wraps it, with its headers private to that module or vendored; no foreign header reaches a header of ours; all other code calls only its own lower layers and the standard library (`DIRECTION-3.md`). What does not meet it today, each a named departure with its end:
  - `tests/child/` and `tests/driver/common.cpp`: end at P4.
  - SQLite's two handle types and public engine header: end at step (d), where SQLite has one owner in `system/`,
    the engine and its wrapper, the engine's header private and the handles opaque.
- A contract is its module's public headers, in `include/qiven/<module>/`; each built module's `contract` field names its entry header, or is null where it has none. Where adapters and fakes stand behind it, as platform's and compute's, it is stated as concepts, which every adapter and fake meets; where none does, as in core and diag, it is the headers' types and functions.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
