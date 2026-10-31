# test

layer devkit, status built

- **Holds:** the test runner, the faults kit (`qiven3_faults`, `tests/faults/`), the scan, the drives' Python session and the ctest listing
- **Never holds:** a product's fixtures, vectors or store books; a test of one module's behaviour
- **Contract:** `include/qiven/test/faults.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** memory, base, platform, matcher, wire, store, entry, secret, checked, location, write
- **Consumers rely on:** an operation run with its k-th allocation or platform call failed, for k from 1 until it completes; each case's log; the runner's cases by name
- **Consumers give:** none
- **Refuses:** a fault run that does not end in the operation's typed error: the case fails
- **Words:**
  - `fault run`: one run of an operation with its k-th allocation or platform call failed
