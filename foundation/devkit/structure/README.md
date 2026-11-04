# structure

layer devkit, status built

- **Awaiting a split:** its product data (qiven3's paths, lists and cells, in `lists.py`, `rules.py` and `exemptions.py`) leaves for `products/qiven3`; its row's edges to qiven3's modules are the product's header units' (`tests/support/headers/`, `qiven3_headers` and `qiven3_test_headers`), which leave its row for a row of the product's tests table, their edges and its exception with them; the foundation's units are their modules' (each in its module's `tests/support/headers/`)
- **Holds:** the structure method in Python: the lexer, the records, the subset check, the rules for any code, and the content check; the product's header units
- **Never holds:** a product's own paths, lists or cells as the method's code
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.structure`; its tests, under `tests/`, run by unittest
- **Files:** the method's modules at this directory's top, its tests under `tests/`, and `CMakeLists.txt`, the structure preset's records step; the product's header units are the row's other path, `tests/support/headers/`
- **Consumers rely on:** every tracked file read against the rules; a finding names its file and rule
- **Parameters:** none
- **Refuses:** a file that breaks a rule: its test fails
