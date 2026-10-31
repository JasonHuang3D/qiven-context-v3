# platform

layer system, status built

- **Awaiting a split:** its product halves leave for `products/qiven3`: `facts` (BitLocker, file history and the volume's protection), `synced` (the sync roots) and `residual` (the residual rows), today in the contract (`src/platform/platform.hpp`) and the Windows adapter (`src/platform/win/facts.cpp`, `facts_calls.cpp`, `synced.cpp`)
- **Holds:** the platform contract (files, processes, time, pages, random bytes), its Windows adapter, its fake, and `current`, the system's own adapter, which the build chooses by its target system; the product's `app`, not this module, chooses among the adapters one system offers
- **Never holds:** a store, entry or location; a product's checks over the machine but `facts`, `synced` and `residual`, until they leave
- **Contract:** `src/platform/platform.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's header moves to `include/qiven/platform/platform.hpp`.
- **Consumers rely on:** concepts bound at compile time; views in, the caller's buffers out, on the caller's arena; typed `PlatformError` codes, never a system's number
- **Parameters:** `config_leaf` (not built yet); `open_bound` (not built yet)
- **Refuses:** a system it has no adapter for (`unsupported`); a stage past `open_bound` files already open
- **Words:**
  - `adapter`: a concrete type meeting `Platform` for one system; the fake is one
  - `stage`: a staged directory's files, written then replaced whole
