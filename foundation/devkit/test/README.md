# test

layer devkit, status built

- **Awaiting a split:** it becomes a group of four modules, `runner`, `case_main`, `faults` and `scan`, one target each; the runner's own files today in log's row (`tests/runner/runner.hpp`, `runner_test.cpp`, `runner_demo.cpp`, `tests/drive/test_runner.py`) join `runner`, and `tests/drive/test_entry.py`, a test of diag's entry today in its `faults` row, moves to diag's tests
- **Holds:** the runner's core (`tests/runner/core.hpp`, `runner.cpp`), the faults kit (the target `qiven3_faults`, `tests/support/faults/`, `tests/faults/`), the scan, the drives' Python session and the ctest listing
- **Never holds:** a product's fixtures, vectors or store books; a test of one module's behaviour but `tests/drive/test_entry.py`, until it moves
- **Contract:** `tests/support/faults/faults.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's header moves to `include/qiven/test/faults.hpp`.
- **Consumers rely on:** an operation run with its k-th allocation or platform call failed, for k from 1 until it completes; each case's log; the runner's cases by name
- **Parameters:** none
- **Refuses:** a fault run that does not end in the operation's typed error: the case fails
- **Words:**
  - `fault run`: one run of an operation with its k-th allocation or platform call failed
