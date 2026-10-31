# ai

layer kits, status slot

- **Will hold:** model clients, embeddings and prompt assembly
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/<kit>/ai/ai.hpp`, the kit naming itself (its namespace, target and include root its own, not the foundation's `qiven`), stating a model client, an embedding and a prompt as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services, kits
- **Parameters:** none
- **Filling it:** its code, a target in the kit's own name, tests labelled `ai`, status set to `built`; no other module changes
