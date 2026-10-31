# posix

layer system, status slot

- **Will hold:** the platform's adapter for POSIX systems
- **Never holds:** a product's concept; another system's header
- **Contract a filler meets:** meets the contract of `platform`, `include/qiven/platform/platform.hpp`, as built: the `Platform` concept and its value types; it passes the one contract suite that runs over every adapter and fake of `platform`, and changes no line of the contract
- **May depend on:** devkit, core, system
- **Parameters:** none
- **Filling it:** its code, built under the target `qiven_platform_posix`, its tests labelled `platform`, its system added to the `platforms` of its own module.json, status set to `built`; the build chooses it by the target system, inside the platform module, so no module changes to pick it
