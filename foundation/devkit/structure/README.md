# structure

layer devkit, status built

- **Awaiting a split:** its product data (qiven3's paths, lists and cells, in `lists.py`, `rules.py` and `subset.py`, and in `tests/test_layers.py` and `tests/test_links.py`) leaves for `products/qiven3`; its row's edges to qiven3's modules are the product's header units' (`tests/support/headers/`, `qiven3_headers` and `qiven3_test_headers`), each of which goes with its header's module when that module moves, in P4, its edges with it; the row's path and its exception stay until then, and an old place's path joins no row; the foundation's units are their modules' (each in its module's `tests/support/headers/`)
- **Holds:** the structure method in Python: the lexer, the records, the subset check, the rules for any code, the content check, and the modules' check (`modules.py`, the table kept true of the checkout, whose command line is `tools/loop_modules.py`); among the rules' tests, K11 (`tests/test_binaries.py`), each binary of a build held to Windows' system libraries, their API sets or its preset's residual, the lists K11's own there, each binary read by `foundation/devkit/python/imports.py`, and the Python import check (`tests/test_imports.py`): the standard library, the test tree's, this method's, the runner's Python half and Python's lowest layer alone, no `importlib` or `runpy`, no `ctypes`, `winreg` or `_winapi` but in `foundation/devkit/python/_windows.py`, and no change of `sys.path` but its `append`; the product's header units
- **Never holds:** a product's own paths, lists or cells as the method's code
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.structure`; its tests, under `tests/`, run by unittest
- **Files:** the method's modules at this directory's top, its tests under `tests/`, and `CMakeLists.txt`, the structure preset's records step; the product's header units are the row's other path, `tests/support/headers/`
- **Consumers rely on:** every tracked file read against the rules; a finding names its file and rule
- **Parameters:** none
- **Refuses:** a file that breaks a rule: its test fails
