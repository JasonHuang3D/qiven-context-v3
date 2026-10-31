# kv

layer services, status slot

- **Will hold:** a key-value store over bytes
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/qiven/kv/kv.hpp`, stating a key-value store and its iteration as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services
- **Parameters:** none
- **Filling it:** its code, a target `qiven_kv`, tests labelled `kv`, status set to `built`; no other module changes
