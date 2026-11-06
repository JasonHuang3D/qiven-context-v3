# structure

layer devkit, status built

Version 0.3: the number of this file's change list, below, and of nothing else

- **Awaiting a split:** its product data (qiven3's paths, lists and cells, in `lists.py`, `rules.py` and `subset.py`, the angle include of `qiven3/` in `modules.py`'s `INCLUDE`, and in `tests/test_line_endings.py`'s product files kept byte for byte and their directories) leaves for `products/qiven3`; its row's edges to qiven3's modules are the product's header units' (`tests/support/headers/`, `qiven3_headers` and `qiven3_test_headers`), each of which goes with its header's module when that module moves, in P4, its edges with it; the row's path and its exception stay until then, and an old place's path joins no row; the foundation's units are their modules' (each in its module's `tests/support/headers/`)
- **Holds:** the structure method in Python:
  - the lexer (`lexer.py`), which `build`'s header units read too, and the records (`records.py`, with `CMakeLists.txt`, the structure preset's step that writes them);
  - the rules for any code: the checker (`rules.py`), its lists as data (`lists.py`), the subset's token rules (`subset.py`), the tree's files through `python`'s `git` (`tree.py`), and a tree's modules and layers (`layers.py`);
  - the content check (`content.py`, `content_findings`) and B15's size check (`size.py`, `size_findings`);
  - the modules' check (`modules.py`, the table kept true of the checkout), whose command line is `tools/loop_modules.py`;
  - the Python import check (`python_imports.py`, `python_findings`, run by `tests/test_imports.py`): the standard library, the test tree's, this method's, `build`'s two readers its tests use (`links.py`, `exemptions.py`), `harness` and Python's lowest layer alone, no `importlib` or `runpy`, no `ctypes`, `winreg` or `_winapi` but in `foundation/devkit/python/_windows.py`, and no change of `sys.path` but its `append`;
  - `tests/test_triggers.py`, CI's triggers and its unpinned images, the repository's cases, kept here: the repository's row has no tree, and one file of two cases does not justify a walked tree and a layer change (ARCH-037); an item for this module's end challenge and the test framework's design;
  - the product's header units
- **Never holds:** a product's own paths, lists or cells as the method's code
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.structure`, imported normally; its tests, under `tests/`, run by unittest, the rules' as one test, a subtest a rule, labelled `fast`, 60 s each; `tools/loop_modules.py SOURCE [BUILD]`; `records.py SOURCE BUILD CMAKE DUMPBIN`, the records' step
- **Files:** the method's modules at this directory's top, its tests under `tests/`, and `CMakeLists.txt`; the product's header units are the row's other path, `tests/support/headers/`
- **Consumers rely on:** every tracked file read against the rules; a finding names its file and rule; `build`'s configure, the lexer; `tools/loop_modules.py` and `tools/loop_base.py`, `modules`; `tests/drive/test_differential.py`, `content.PATTERNS`
- **Parameters:** none
- **Refuses:** a file that breaks a rule: its test fails. `CMakeLists.txt`, off MSVC or with no interpreter: the configure stops. `tools/loop_modules.py`: errors in the table, each one line and status 1; a wrong usage, or a check that cannot run, status 2

The method, once, for the module, which wraps nothing:

- **Compatibility:** Windows built. The records read MSVC's compile commands and `dumpbin`, Windows' alone; the Python itself is `pathlib`, `ast` and `python`'s wraps; no Linux build
- **Extensibility:** a new rule is a row of `lists.py` or `subset.py` and a branch of its checker, an allowance a row with its reason, and a rule with no consumer goes; a new tree is a value of `tools/trees.json`
- **Performance:** the structure preset at 12 jobs, alone, measured once at `9d4dd9c`: 17 tests in 13.46 s, the slowest 12.64 s; a test past 60 s is killed, and a kill under other builds' load is read as load first, by one rerun alone
- **A fake:** none: each rule reads the real checkout and the real build's records, with its planted controls in its tests; the rules' control is `tests/test_boundaries.py`'s last subtest, which fails on each allowance row that allowed nothing over the tree, by name (`rules.ROWS`, as `build`'s `exemptions.py` does for its list); a tool or a check here is proven by use
- **Errors:** a finding is a test's failure, with its file and rule. A record that is stale after a C++ change reads as a failing Tree test until `cmake --build build/structure` runs. `tools/loop_modules.py .` with no build reported 0 errors where the links gave 14 (the kit's rows cut of `text` and `diag`), read at `73b57fc` by (b)'s design challenge, 2026-11-06; at `ee70977`, 2026-11-07, both gave 0 errors, `build/ci-windows` given. The links' half reads the build's codemodel, so a change to a row's edges runs it with `BUILD` given

## Changes

Newest first, each entry its version, its date and the commit it describes.

### 0.3, 2026-11-07, the commit that brings this entry

The rules' control, by the end challenges' St-4 (A).

- **This file:** A fake names the control.
- **Members changed:** `rules.allowed()` becomes `allows()`, the allowance rows that allow a finding; `rules.Findings` records them (`used`); `rules.ROWS` holds every row, of `ALLOWED`, `EXEMPT` and `ALLOWANCES`; `rules.check()` and `rules.findings()` give the `Findings`, not its set. `tests/test_boundaries.py` asserts each row used.
- **Members removed:** the allowance rows that allowed nothing: B1's vendored files, which the rules already skip; B19's `arena.` and `pool.` headers, B25's and B31's `fail.` header, and B30's `platform/windows/` headers, under `include/`; B5's `qiven_diag_poisoned`, a target the structure preset never builds (a sanitizer's alone); and B1's link rule's own exception of `qiven_storage_sqlite`, which `ALLOWED`'s row now does.
- **What a consumer does:** reads the findings from `.found`; `build`'s `test_exemptions.py` uses `rules.ROOT` and `source_files` only, and does nothing.
- **Known limits:** the control proves that each allowance allowed something, not that a rule finds what it should.

### 0.2, 2026-11-07, `f78c42d`

Each case to its subject's row, by the end challenges' St-3 with Bu-6.

- **This file:** `links.py`, `exemptions.py` and K11 leave the Holds list; the Python import check names `build`'s two readers; the triggers' line says why they stay; the awaiting list no longer names `tests/test_links.py`.
- **Members moved:** `links.py` and `exemptions.py` to `build` (`foundation/devkit/build/`), with their tests and the cases of build's subjects: the floors (`test_floors.py`), K11 (`test_binaries.py`), the links (`test_links.py`), the analysis controls (`test_analysis.py`) and the exemptions (`test_exemptions.py`), each whole. The import check allows `foundation.devkit.build`.
- **What a consumer does:** imports `links` and `exemptions` from `foundation.devkit.build`; selects the moved cases by `build`'s label, not this module's name. The structure preset no longer runs them.
- **Known limits:** `tests/test_triggers.py` stays until the repository's row can hold a test.

### 0.1, 2026-11-06, `cdf16f1`

The charter in the method's form, by step (b)'s design challenge.

- **This file:** the method stated once, a version and this list; each file of the method named, `tree.py`, `layers.py`, `lists.py`, `links.py` and `exemptions.py` among them; the Interface, consumers and refusals as they now stand.
- **What a consumer does:** nothing: no member changes.
- **Known limits:** `tools/loop_modules.py`'s no-build reading, under Errors above; the awaiting list stands until P4.
