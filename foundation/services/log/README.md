# log

layer services, status built

- **Awaiting a split:** three parts leave: the drive's events (`drive.request`, `drive.reply` in `src/log/catalogue.hpp`) for qiven3's own catalogue; the ring (`src/log/ring.hpp`, the crash buffer diag's handler reads) for `foundation/system/diag`; the test runner's files in its row (`tests/runner/runner.hpp`, `runner_test.cpp`, `runner_demo.cpp`, `tests/drive/test_runner.py`) for devkit's `runner` module
- **Holds:** levelled JSON lines into a crash ring, events by descriptor of a catalogue, operations' ids, the drain
- **Never holds:** run-time text in a line; a product's events but the drive's two, until they leave
- **Contract:** `src/log/log.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's header moves to `include/qiven/log/log.hpp`.
- **Consumers rely on:** fields of four types alone, a name made in constant evaluation from a literal, so no run-time text reaches a line; an event never throws, a failed allocation drops it and counts it
- **Parameters:** `catalogue` (not built yet)
- **Refuses:** an event outside a catalogue; a field of another type; an event below the build's minimum compiles to nothing
- **Words:**
  - `catalogue`: the events a binary may write, each a descriptor of a name, a level and fields
  - `ring`: the bounded buffer of the last lines, read on a crash
