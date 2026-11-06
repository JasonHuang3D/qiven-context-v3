# window

layer system, status slot

- **Will hold:** windows and their input events
- **Never holds:** a product's concept; a system's header outside its adapters
- **Contract a filler meets:** the entry header `include/qiven/window/window.hpp`, stating a window, its events and its surface as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** devkit, core, system
- **Parameters:** none
- **Filling it:** its code, a target `qiven_window`, tests labelled `window`, status set to `built`; no other module changes
