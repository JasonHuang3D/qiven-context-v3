# simd

layer core, status slot

- **Will hold:** portable vector operations over bytes and numbers
- **Never holds:** a system call, I/O, or a product's concept; an intrinsic outside its own sources
- **Contract a filler meets:** the entry header `include/qiven/simd/simd.hpp`, stating its vector types and operations, with a scalar path for each as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core
- **Expected consumers:** math, codec, digest
- **Filling it:** its code, a target `qiven_simd`, tests labelled `simd`, status set to `built`; no other module changes
