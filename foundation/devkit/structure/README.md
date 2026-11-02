# structure

layer devkit, status built

- **Awaiting a split:** its product data (qiven3's paths, lists, plants and cells, in `rules.py`, `exemptions.py` and `plants.py`) leaves for `products/qiven3`, and with it its row's edges to qiven3's modules
- **Holds:** the structure method in Python: the lexer, the records, planting, the subset check, the rules for any code, and the content check; the header units
- **Never holds:** a product's own paths, lists, plants or cells as the method's code
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.structure`; its tests, under `tests/`, run by unittest
- **Files:** the method's modules at this directory's top, its tests under `tests/`, and `CMakeLists.txt`, the structure preset's records step; the header units are the row's other path, `tests/support/headers/`
- **Consumers rely on:** every tracked file read against the rules; a finding names its file and rule
- **Parameters:** none
- **Refuses:** a file that breaks a rule: its test fails
