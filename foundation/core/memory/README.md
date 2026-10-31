# memory

layer core, status built

- **Holds:** the allocator type and containers over an arena, pools, the budget's error, bounded text, the lock, checked sizes, and the failing countdown under `QIVEN_FAULTS`
- **Never holds:** a system call, I/O, or a product's concept
- **Contract:** `src/memory/memory.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's header moves to `include/qiven/memory/memory.hpp`.
- **Consumers rely on:** single, scoped ownership: an arena lends its bytes to one operation's containers and takes them all back at its end; an allocation past its budget is the budget's error
- **Parameters:** none
- **Refuses:** an allocation past its budget; a size whose check fails
- **Words:**
  - `arena`: a region that lends bytes to one operation and takes them back at its end
  - `budget`: the bytes an arena may lend; past it, the budget's error
