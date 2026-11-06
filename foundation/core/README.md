# core

layer core, status group

Values and algorithms over bytes, with no system call and no I/O.

- Depends on the standard library and core alone, but memory's private `src/poison.hpp`, which under AddressSanitizer includes the sanitizer's interface: memory owns it, and no sanitizer macro reaches a public header. An edge goes down only; within a layer a module depends on another only by a declared edge; the graph has no cycle.
- A dependency on what is not ours (an operating system, a third-party library, an SDK) is ours to own: one foundation module wraps it, with its headers private to that module or vendored; no foreign header reaches a header of ours; all other code calls only its own lower layers and the standard library (`DIRECTION-3.md`). No named departure touches this layer; they are in `foundation/README.md`.
- Compiles on any platform. No conditional on the system; a static size pin guarded by the compiler's library layout (`_MSC_VER`, `_WIN64`, the iterator debug level) is none: it checks a layout and changes no code. `memory.hpp` chooses two spellings by the compiler: `QIVEN_THREAD_ATTRIBUTE` on `__clang__`, and `QIVEN_NO_UNIQUE_ADDRESS` on `_MSC_VER`, which changes a layout.
- A contract here is the module's public headers: their types and functions, with one concept, `KeyRule`; core has no adapters and no fakes.

Each subdirectory holding a module.json is one module or slot; its README.md is its charter.
