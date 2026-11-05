# faults

layer devkit, status built

- **Awaiting its move:** the drive of its row that tests another module's behaviour stays under `tests/` until its module's move takes it: `tests/drive/test_hardened.py` (build's: the standard library's hardened mode).
- **Holds:** the target `qiven_faults`: fault injection's loop, a header over memory's countdown writing each failure it injects to the case's log (`qiven/faults/faults.hpp`), and a fault run's parts (`qiven/faults/parts.hpp`); until its move, the drive of a binary's hardening
- **Never holds:** a module's fault seam (memory's failing countdown, the platform's failing calls), that module's own code; a test of one module's behaviour but the drive above (`tests/drive/test_hardened.py`), held until its module's move takes it
- **Contract:** `foundation/devkit/test/faults/include/qiven/faults/faults.hpp`
- **Files:** `include/qiven/faults/` (`faults.hpp`, `parts.hpp`), `CMakeLists.txt`; their namespace `qiven::test`; the drive above, still a `present` path of module.json under `tests/`
- **Consumers rely on:** an operation run with its k-th allocation or platform call failed, for k from 1 until it completes; each case's log
- **Parameters:** none
- **Refuses:** a fault run that does not end in the operation's typed error: the case fails
- **Words:**
  - `fault run`: one run of an operation with its k-th allocation or platform call failed
