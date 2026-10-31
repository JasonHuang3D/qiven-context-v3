# macos

layer system, status slot

- **Will hold:** the platform's adapter for macOS
- **Never holds:** a product's concept; another system's header
- **Contract a filler meets:** meets the contract of `platform`, `include/qiven/platform/platform.hpp`, as built: the `Platform` concept and its value types; it passes the one contract suite that runs over every adapter and fake of `platform`, and changes no line of the contract
- **May depend on:** devkit, core, system
- **Expected consumers:** current
- **Filling it:** its code, built under the target `qiven_platform_macos`, its tests labelled `platform`, its platform added to `platforms`, status set to `built`; no other module changes
