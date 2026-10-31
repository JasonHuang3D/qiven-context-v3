# blob

layer services, status slot

- **Will hold:** a store of large byte objects by key
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/qiven/blob/blob.hpp`, stating a blob store, its reads and writes by range as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services
- **Expected consumers:** the kits, the products
- **Filling it:** its code, a target `qiven_blob`, tests labelled `blob`, status set to `built`; no other module changes
