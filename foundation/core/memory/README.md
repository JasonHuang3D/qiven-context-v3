# memory

layer core, status built

- **Awaiting the analysis:** its header's unit for the analysis, `tests/support/headers/memory_poison.cpp` (over `qiven/memory/poison.hpp`, a path of structure's row), stays under `tests/support/headers/` until the analysis is taken up after the foundation's reconstruction; its place is settled then, with `poison.hpp`'s, public today for that unit alone and private once the unit is memory's.
- **Holds:** the target `qiven_memory`: the allocator type and containers over an arena, pools, the budget's error, bounded text, the lock, checked sizes, and the failing countdown under `QIVEN_FAULTS`; its own tests (`tests/`): the cases of memory, arena, pool and its frees, the demos of fail_fast and of the frees that end the process, and their drives
- **Never holds:** a system call, I/O, or a product's concept
- **Contract:** `foundation/core/memory/include/qiven/memory/memory.hpp`
- **Files:** `include/qiven/memory/` (`memory.hpp`, `arena.hpp`, `pool.hpp`, `fail.hpp`, `poison.hpp`), `src/` (`memory.cpp`, `arena.cpp`, `pool.cpp`, `fail.cpp`), `tests/`, `CMakeLists.txt`, `.clang-tidy`; their namespace `qiven::memory`, their macros `QIVEN_`
- **Consumers rely on:** single, scoped ownership: an arena lends its bytes to one operation's containers and takes them all back at its end; an allocation past its budget is the budget's error
- **Parameters:** none
- **Refuses:** an allocation past its budget; a size whose check fails
- **Words:**
  - `arena`: a region that lends bytes to one operation and takes them back at its end
  - `budget`: the bytes an arena may lend; past it, the budget's error
