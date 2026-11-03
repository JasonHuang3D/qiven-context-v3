# text

layer core, status built

- **Awaiting the analysis:** its header's unit for the analysis, `tests/support/headers/text_json_bytes.cpp` (over `qiven/text/json_bytes.hpp`), stays under `tests/support/headers/` until the analysis is taken up after the foundation's reconstruction; its place is settled then.
- **Awaiting a split:** module.json holds a second row, `base` (digest and the format's names in `src/qiven3/`, their tests, and `tests/support/fixtures/filled.hpp`), which digest's and format's `from` name, until each of its parts leaves for its own module
- **Holds:** the target `qiven_text`: `utf` (strict UTF-8, code points and UTF-16 units, code point order, a system name's lone surrogates written as backslashreplace writes them, and the parser's codes, `ParseCode`, and typed error, `ParseError`), `json` (JSON values, their scan and the canonical encoder) and `canon` (the strict parser and canonical writer over them, a key's rule bound at compile time), and their instantiation over memory's allocator; its own tests (`tests/`): utf's, json's and canon's vectors and rules, their budgets and fault runs, and a subset of their vectors evaluated by the compiler
- **Never holds:** a format's or product's keys, a system call, or I/O
- **Contract:** `foundation/core/text/include/qiven/text/canon.hpp`
- **Files:** `include/qiven/text/` (`utf.hpp`, `json.hpp`, `json_bytes.hpp`, `canon.hpp`), `src/` (`utf.cpp`, `json.cpp`, `canon.cpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::text` (`ParseCode`, `ParseError`), with `qiven::text::utf`, `qiven::text::json` and `qiven::text::canon`
- **Consumers rely on:** strict reading: one code per refusal (`ParseCode`); canonical bytes: keys sorted, integers within 2^53, no float, no null
- **Parameters:** none
- **Refuses:** invalid UTF-8, a BOM, a JSON text past its depth, a float, a null, an integer out of range, a duplicate key, non-canonical bytes where canonical are asked
- **Words:**
  - `canonical`: the one byte form of a value: keys sorted, no space, integers in decimal
