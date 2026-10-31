# math

layer core, status slot

- **Will hold:** numeric types and functions: vectors, matrices, fixed and checked arithmetic
- **Never holds:** a system call, I/O, or a product's concept
- **Contract a filler meets:** the entry header `include/qiven/math/math.hpp`, stating its number types and their operations as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core
- **Parameters:** none
- **Filling it:** its code, a target `qiven_math`, tests labelled `math`, status set to `built`; no other module changes
