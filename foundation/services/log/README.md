# log

layer services, status built

- **Holds:** levelled JSON lines into a crash ring, events by descriptor of a catalogue, operations' ids, the drain
- **Never holds:** run-time text in a line; a product's events
- **Contract:** `include/qiven/log/log.hpp`; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** faults, app, driver, child
- **Consumers rely on:** fields of four types alone, a name made in constant evaluation from a literal, so no run-time text reaches a line; an event never throws, a failed allocation drops it and counts it
- **Consumers give:** `catalogue`
- **Refuses:** an event outside a catalogue; a field of another type; an event below the build's minimum compiles to nothing
- **Words:**
  - `catalogue`: the events a binary may write, each a descriptor of a name, a level and fields
  - `ring`: the bounded buffer of the last lines, read on a crash
