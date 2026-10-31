# sync

layer services, status slot

- **Will hold:** the import of records into a record log that is not empty
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/qiven/sync/sync.hpp`, stating an import that adds records to a store holding some as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services
- **Parameters:** none
- **Filling it:** its code, a target `qiven_sync`, tests labelled `sync`, status set to `built`; no other module changes
