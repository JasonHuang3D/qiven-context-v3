# codec

layer core, status slot

- **Will hold:** encodings over bytes: base64, hex, binary framing
- **Never holds:** a system call, I/O, or a product's concept
- **Contract a filler meets:** the entry header `include/qiven/codec/codec.hpp`, stating an encoder and a decoder over spans as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core
- **Expected consumers:** transport, storage, the kits
- **Filling it:** its code, a target `qiven_codec`, tests labelled `codec`, status set to `built`; no other module changes
