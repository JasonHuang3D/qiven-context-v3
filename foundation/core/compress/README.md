# compress

layer core, status slot

- **Will hold:** compression and decompression over bytes
- **Never holds:** a system call, I/O, or a product's concept
- **Contract a filler meets:** the entry header `include/qiven/compress/compress.hpp`, stating an encoder and a decoder over spans as concepts; an archetype of each concept in its tests; one contract suite run over its implementation: core has no adapters and no fakes
- **May depend on:** core
- **Parameters:** none
- **Filling it:** its code, a target `qiven_compress`, tests labelled `compress`, status set to `built`; no other module changes
