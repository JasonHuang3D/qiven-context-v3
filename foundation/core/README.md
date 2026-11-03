# core

layer core, status group

Values and algorithms over bytes, with no system call and no I/O.

- Depends on the standard library and core alone. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- Compiles on any platform. No system header and no conditional on the system; a static size pin guarded by the compiler's library layout (`_MSC_VER`, `_WIN64`, the iterator debug level) is none: it checks a layout and changes no code.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
