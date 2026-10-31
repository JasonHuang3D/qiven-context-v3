# faults

layer devkit, status built

- **Awaiting its move:** its files are still the `present` paths of module.json, under `tests/`; they move into this directory, its contract's header to `include/qiven/faults/faults.hpp`. `tests/drive/test_entry.py`, a test of diag's entry today in its row, moves to diag's tests.
- **Holds:** the faults kit (the target `qiven3_faults`, `tests/support/faults/`, `tests/faults/`) and the drives of a binary's faults, hardening, hangs and debug runs
- **Never holds:** a module's fault seam (memory's failing countdown, the platform's failing calls), that module's own code; a test of one module's behaviour but `tests/drive/test_entry.py`, until it moves
- **Contract:** `tests/support/faults/faults.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json.
- **Consumers rely on:** an operation run with its k-th allocation or platform call failed, for k from 1 until it completes; each case's log
- **Parameters:** none
- **Refuses:** a fault run that does not end in the operation's typed error: the case fails
- **Words:**
  - `fault run`: one run of an operation with its k-th allocation or platform call failed
