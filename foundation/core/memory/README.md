# memory

layer core, status built

- **Holds:** the target `qiven_memory`: the allocator type and containers over an arena, pools and `Heap` with `heap_wait` (`pool.hpp`), the budget's error, bounded text, the lock, checked sizes, core's fail-fast routine (`fail_fast`, `fail_cause`, `fail.hpp`), and the failing countdown under `QIVEN_FAULTS`; under `QIVEN_SYSTEM_HEAP`, each block its own allocation of the system heap; under AddressSanitizer without it, the poisoning (`src/poison.hpp`, the one includer of the sanitizer's interface); its own tests (`tests/`): the cases of memory, arena, pool and its frees, the demos of fail_fast and of the frees that end the process, and their drives
- **Never holds:** a system call, I/O, or a product's concept
- **Contract:** `foundation/core/memory/include/qiven/memory/memory.hpp`
- **Files:** `include/qiven/memory/` (`memory.hpp`, `arena.hpp`, `pool.hpp`, `fail.hpp`), `src/` (`memory.cpp`, `arena.cpp`, `pool.cpp`, `fail.cpp`, `poison.hpp`), `tests/`, `CMakeLists.txt`, `.clang-tidy`; their namespace `qiven::memory`, their macros `QIVEN_`
- **Consumers rely on:** an arena's single, scoped ownership: it lends its bytes to one operation's containers and takes them all back at its end; an allocation past its budget is the budget's error; `fail_fast` ends the process at once, in every build; `Heap` and `heap_wait` as `pool.hpp` states them
- **Parameters:** none
- **Refuses:** an allocation past its budget; a size whose check fails
- **Words:**
  - `arena`: a region that lends bytes to one operation and takes them back at its end
  - `budget`: the bytes an arena may lend; past it, the budget's error
