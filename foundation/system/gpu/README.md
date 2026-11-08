# gpu

layer system, status slot

- **Will hold:** the GPU device behind its own contract (a device, its queues and its memory), for compute and for graphics
- **Never holds:** a product's concept; a system's header outside its adapters; anything of `compute`: compute's GPU backend lives inside `foundation/services/compute`, built over this module's contract
- **Contract a filler meets:** its own device contract, the entry header `include/qiven/gpu/gpu.hpp`, stating a device and its queues as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** devkit, core, system
- **Parameters:** none
- **Filling it:** its code, a target `qiven_gpu`, tests labelled `gpu`, status set to `built`; no other module changes
