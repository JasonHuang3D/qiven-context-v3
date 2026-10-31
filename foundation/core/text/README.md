# text

layer core, status built

- **Holds:** `utf` (strict UTF-8), `json` (JSON values and the canonical encoder) and `canon` (the strict parser and canonical writer over them)
- **Never holds:** a format's or product's keys, a system call, or I/O
- **Contract:** `src/qiven3/canon.hpp`
- **Files:** not yet in this directory: its files are the `present` paths of module.json; its contract's headers move to `include/qiven/text/utf.hpp`, `include/qiven/text/json.hpp`, `include/qiven/text/canon.hpp`.
- **Consumers rely on:** strict reading: one code per refusal (`ParseCode`); canonical bytes: keys sorted, integers within 2^53, no float, no null
- **Parameters:** none
- **Refuses:** invalid UTF-8, a BOM, a JSON text past its depth, a float, a null, an integer out of range, a duplicate key, non-canonical bytes where canonical are asked
- **Words:**
  - `canonical`: the one byte form of a value: keys sorted, no space, integers in decimal
