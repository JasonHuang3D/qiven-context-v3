# compute

layer services, status built

- **Holds:** the compute contract (CPU threads, vector instructions, GPGPU) and its fake
- **Never holds:** a caller's features; a device SDK's header (a device backend, an adapter slot inside this module, reaches its device through that device module's own contract, such as `gpu`'s)
- **Contract:** `src/hal/hal.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's header moves to `include/qiven/compute/hal.hpp`.
- **Consumers rely on:** concepts bound at compile time; views in, results into the caller's out; a buffer a move-only value; a capability's absence a `Gap` value, never an error
- **Parameters:** none
- **Refuses:** a failed operation, as `HalError`; a device or buffer this compute did not give
- **Words:**
  - `Gap`: a capability's absence, a value
