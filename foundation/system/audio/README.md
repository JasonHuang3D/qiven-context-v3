# audio

layer system, status slot

- **Will hold:** audio output and input devices
- **Never holds:** a product's concept; a system's header outside its adapters
- **Contract a filler meets:** the entry header `include/qiven/audio/audio.hpp`, stating a device, a stream and its buffers as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** devkit, core, system
- **Parameters:** none
- **Filling it:** its code, a target `qiven_audio`, tests labelled `audio`, status set to `built`; no other module changes
