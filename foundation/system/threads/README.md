# threads

layer system, status slot

- **Will hold:** threads: a stop token, a bounded queue, a name
- **Never holds:** a product's concept; a system's header outside its adapters
- **Contract a filler meets:** the entry header `include/qiven/threads/threads.hpp`, stating a thread, its stop token and a bounded queue as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** devkit, core, system
- **Parameters:** none
- **Filling it:** its code, a target `qiven_threads`, tests labelled `threads`, status set to `built`; no other module changes
