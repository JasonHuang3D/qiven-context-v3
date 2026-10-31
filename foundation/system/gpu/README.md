# gpu

layer system, status slot

- **Will hold:** compute's device backends, and graphics devices
- **Never holds:** a product's concept; a system's header outside its adapters
- **Contract a filler meets:** the entry header `include/qiven/gpu/gpu.hpp`, stating a device and its queues, meeting `compute`'s contract for compute as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** devkit, core, system
- **Expected consumers:** compute, the scene kit
- **Filling it:** its code, a target `qiven_gpu`, tests labelled `gpu`, status set to `built`; no other module changes
