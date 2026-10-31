# telemetry

layer services, status slot

- **Will hold:** metrics and traces over log
- **Never holds:** a product's concept; a system header
- **Contract a filler meets:** the entry header `include/qiven/telemetry/telemetry.hpp`, stating a counter, a gauge, a span and a sink as concepts; an archetype of each concept in its tests; one contract suite run over every adapter and fake
- **May depend on:** core, system, services
- **Expected consumers:** the products
- **Filling it:** its code, a target `qiven_telemetry`, tests labelled `telemetry`, status set to `built`; no other module changes
