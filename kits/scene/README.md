# scene

layer kits, status slot

- **Will hold:** a scene graph, its assets and its rendering
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/<kit>/scene/scene.hpp`, the kit naming itself (its namespace, target and include root its own, not the foundation's `qiven`), stating a scene, its nodes and assets as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services, kits
- **Parameters:** none
- **Filling it:** its code, a target in the kit's own name, tests labelled `scene`, status set to `built`; no other module changes
