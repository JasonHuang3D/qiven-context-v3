# platform

layer system, status built

- **Holds:** the platform contract (files, processes, time, pages, random bytes), its Windows adapter, its fake, and `current`, which makes the adapter for the build's system
- **Never holds:** a product's checks over the machine; a store, entry or location
- **Contract:** `include/qiven/platform/platform.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** base, wire, store, entry, checked, location, write, driver, child, tooling
- **Consumers rely on:** concepts bound at compile time; views in, the caller's buffers out, on the caller's arena; typed `PlatformError` codes, never a system's number
- **Consumers give:** `config_leaf`, `open_bound`
- **Refuses:** a system it has no adapter for (`unsupported`); a stage past `open_bound` files already open
- **Words:**
  - `adapter`: a concrete type meeting `Platform` for one system; the fake is one
  - `stage`: a staged directory's files, written then replaced whole
