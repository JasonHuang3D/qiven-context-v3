# business

layer kits, status slot

- **Will hold:** business records, documents and workflows
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/<kit>/business/business.hpp`, the kit naming itself (its namespace, target and include root its own, not the foundation's `qiven`), stating a document, a record and a workflow as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services, kits
- **Expected consumers:** the ERP
- **Filling it:** its code, a target in the kit's own name, tests labelled `business`, status set to `built`; no other module changes
