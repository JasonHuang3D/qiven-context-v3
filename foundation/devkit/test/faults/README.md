# faults

layer devkit, status built

- **Awaiting its move:** the drives of its row that test another module's behaviour stay under `tests/` until their module's move takes them: `tests/faults/` (the diag entry's planted faults, which `tests/drive/test_faults.py` runs) and `tests/drive/test_faults.py`, `test_hardened.py`, `test_hang.py`, `test_debug.py`, `test_entry.py`.
- **Holds:** the target `qiven_faults`: fault injection's loop, a header over memory's countdown writing each failure it injects to the case's log (`qiven/faults/faults.hpp`), and a fault run's parts (`qiven/faults/parts.hpp`); until their moves, the drives of a binary's faults, hardening, hangs and debug runs
- **Never holds:** a module's fault seam (memory's failing countdown, the platform's failing calls), that module's own code; a test of one module's behaviour but `tests/drive/test_entry.py`, until it moves
- **Contract:** `foundation/devkit/test/faults/include/qiven/faults/faults.hpp`
- **Files:** `include/qiven/faults/` (`faults.hpp`, `parts.hpp`), `CMakeLists.txt`; their namespace `qiven3::test`; the drives above, still the `present` paths of module.json under `tests/`
- **Consumers rely on:** an operation run with its k-th allocation or platform call failed, for k from 1 until it completes; each case's log
- **Parameters:** none
- **Refuses:** a fault run that does not end in the operation's typed error: the case fails
- **Words:**
  - `fault run`: one run of an operation with its k-th allocation or platform call failed
