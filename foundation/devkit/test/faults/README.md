# faults

layer devkit, status built

- **Holds:** the target `qiven_faults`: fault injection's loop, a header over memory's countdown writing each failure it injects to the case's log (`qiven/faults/faults.hpp`), and a fault run's parts (`qiven/faults/parts.hpp`)
- **Never holds:** a module's fault seam (memory's failing countdown, the platform's failing calls), that module's own code; a test of one module's behaviour
- **Contract:** `foundation/devkit/test/faults/include/qiven/faults/faults.hpp`
- **Files:** `include/qiven/faults/` (`faults.hpp`, `parts.hpp`), `CMakeLists.txt`; their namespace `qiven::test`
- **Consumers rely on:** an operation run with its k-th allocation or platform call failed, for k from 1 until it completes; each case's log
- **Parameters:** none
- **Refuses:** a fault run that does not end in the operation's typed error: the case fails
- **Words:**
  - `fault run`: one run of an operation with its k-th allocation or platform call failed
