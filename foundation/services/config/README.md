# config

layer services, status slot

- **Will hold:** a program's configuration read and validated
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/qiven/config/config.hpp`, stating a configuration source and typed values with defaults as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services
- **Parameters:** none
- **Filling it:** its code, a target `qiven_config`, tests labelled `config`, status set to `built`; no other module changes
