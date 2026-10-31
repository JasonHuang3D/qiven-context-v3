# jobs

layer services, status slot

- **Will hold:** jobs run on threads: a pool, a scheduler, cancellation
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/qiven/jobs/jobs.hpp`, stating a job, a pool and a cancellation as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services
- **Expected consumers:** the kits, the products
- **Filling it:** its code, a target `qiven_jobs`, tests labelled `jobs`, status set to `built`; no other module changes
