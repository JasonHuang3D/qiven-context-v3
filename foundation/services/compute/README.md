# compute

layer services, status built

- **Holds:** the target `qiven_compute`: the compute contract (CPU threads, vector instructions, GPGPU); its fake beside it, the target `qiven_compute_fake`, on the C++ standard library alone; its own tests (`tests/`), over its fake and the devices, sizes and kernel they write themselves: facts as given, a capability's gap, a failed allocate, upload or download, a name no kernel has, a kernel's CPU reference, facts and device on the caller's arena with their fault run, and a caller's mistake ending the process
- **Never holds:** a caller's code, nor the cases over a caller's use; a device SDK's header (a device backend, an adapter slot inside this module, reaches its device through that device module's own contract, such as `gpu`'s)
- **Contract:** `foundation/services/compute/include/qiven/compute/hal.hpp`
- **Files:** `include/qiven/compute/` (`hal.hpp`, the fake's `fake_hal.hpp`), `src/` (`hal.cpp`, `fake_hal.cpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::compute`, the fake's `qiven::compute::fake`
- **Consumers rely on:** concepts bound at compile time; views in, results into the caller's out; a buffer a move-only value; a capability's absence a `Gap` value, never an error
- **Parameters:** none
- **Refuses:** a failed operation, as `HalError`; a device or buffer this compute did not give
- **Words:**
  - `Gap`: a capability's absence, a value
