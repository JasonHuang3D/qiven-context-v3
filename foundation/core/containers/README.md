# containers

layer core, status slot

- **Will hold:** containers beyond memory's: maps, sets, queues over an arena
- **Never holds:** a system call, I/O, or a product's concept
- **Contract a filler meets:** the entry header `include/qiven/containers/containers.hpp`, stating each container's interface as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core
- **Expected consumers:** any module that stores values
- **Filling it:** its code, a target `qiven_containers`, tests labelled `containers`, status set to `built`; no other module changes
