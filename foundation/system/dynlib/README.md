# dynlib

layer system, status slot

- **Will hold:** loading a dynamic library and its symbols
- **Never holds:** a product's concept; a system's header outside its adapters
- **Contract a filler meets:** the entry header `include/qiven/dynlib/dynlib.hpp`, stating a library handle and a symbol lookup as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** devkit, core, system
- **Expected consumers:** gpu, audio
- **Filling it:** its code, a target `qiven_dynlib`, tests labelled `dynlib`, status set to `built`; no other module changes
