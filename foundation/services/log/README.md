# log

layer services, status built

- **Holds:** levelled JSON lines into a crash ring, diag's (`qiven/diag/ring.hpp`, reached through `qiven_diag`), events by descriptor of a catalogue, its own catalogue (`include/qiven/log/catalogue.hpp`: the events its writer emits, `log.start`, `log.dropped`, `op.begin`, `op.end`, and `error.typed`, a typed error any main writes), operations' ids, the drain
- **Never holds:** run-time text in a line; a consumer's events, each beside its writer
- **Contract:** `foundation/services/log/include/qiven/log/log.hpp`
- **Files:** `include/qiven/log/` (`log.hpp`, `catalogue.hpp`), `src/` (`log.cpp`, `line.hpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::log`, with `qiven::log::events` and `qiven::log::detail`
- **Consumers rely on:** fields of four types alone, a name made in constant evaluation from a literal, so no run-time text reaches a line; an event never throws, a failed allocation drops it and counts it
- **Parameters:** `catalogue`
- **Refuses:** an event outside a catalogue; a field of another type; an event below the build's minimum compiles to nothing
- **Words:**
  - `catalogue`: the events a binary may write, each a descriptor of a name, a level and fields
  - `ring`: the bounded buffer of the last lines, read on a crash; diag's
