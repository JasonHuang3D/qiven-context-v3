# structure

layer devkit, status built

Version 0.6: the number of this file's change list, below, and of nothing else

- **Awaiting a split:** its product data (qiven3's paths, lists and cells, in `lists.py`, `rules.py` and `subset.py`, the angle include of `qiven3/` in `modules.py`'s `INCLUDE`, and in `tests/test_line_endings.py`'s product files kept byte for byte and their directories) leaves for `products/qiven3`; its row's edges to qiven3's modules are the product's header units' (`tests/support/headers/`, `qiven3_headers` and `qiven3_test_headers`), each of which goes with its header's module when that module moves, in P4, its edges with it; the row's path and its exception stay until then, and an old place's path joins no row; the foundation's units are their modules' (each in its module's `tests/support/headers/`)
- **Holds:** the structure method in Python:
  - the lexer (`lexer.py`), which `build`'s header units read too, the records (`records.py`), and the build's steps that write them and apply their rules (`steps.py`, with `CMakeLists.txt`, in every tree that sets `QIVEN_STRUCTURE`: the daily tree and the structure preset's): each object's compile, through `steps.py` as its launcher, its record and `object_rules`; a step a target, `object_rules` over its own objects and `defined_name_rules` over its link closure; the link step, `link_rules` over the graph; a finding fails the build;
  - the rules for any code: the checker (`rules.py`), its two halves `file_rules`, the rules a file's own bytes decide, and the rules over the records, which the steps apply, each allowance row one half's (`half`); its lists as data (`lists.py`), the subset's token rules (`subset.py`), the tree's files through `python`'s `git` (`tree.py`), and a tree's modules and layers (`layers.py`);
  - the checks of one module, `rules.<M>` (`checks.py`): over the files of one module of the table, read against the whole listing, `file_rules`, M's own allowance rows, the content check, the Python imports, B15's size, K15 (`log_includes.py`), the include edges and, on a build that makes the records, the directives of M's C++ files no record reaches, by name (`rules.unreached_rules`); `harness` registers one for each module, labelled M and `structure`, so a change to M or to this module runs it;
  - the content check (`content.py`, `content_findings`) and B15's size check (`size.py`, `size_findings`), each over a set of files;
  - the modules' check (`modules.py`, the table kept true of the checkout), whose command line is `tools/loop_modules.py`; its `check` takes a `subset` whose include edges it reads and `whole`, the table, the ownership and the link edges;
  - the Python import check (`python_imports.py`, `python_findings`, run by each module's check): the standard library, the test tree's, this method's, `build`'s two readers its tests use (`links.py`, `exemptions.py`), `harness`, `gate` (`tools/gate.py`'s module) and Python's lowest layer alone, no `importlib` or `runpy`, no `ctypes`, `winreg` or `_winapi` but in `foundation/devkit/python/_windows.py`, and no change of `sys.path` but its `append`;
  - `tests/test_triggers.py`, CI's triggers and its unpinned images, the repository's cases, kept here: the repository's row has no tree, and one file of two cases does not justify a walked tree and a layer change (ARCH-037); an item for this module's end challenge and the test framework's design;
  - the product's header units
- **Never holds:** a product's own paths, lists or cells as the method's code
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.structure`, imported normally; its tests, under `tests/`, run by unittest, the records' rows' control as one test, labelled `fast`, 60 s each; `checks.Check.test_<M>`, the case id of `rules.<M>`, a subtest a part; `tools/loop_modules.py SOURCE [BUILD]`; `steps.py compile|libraries|target|links ...`, the build's steps (its docstring)
- **Files:** the method's modules at this directory's top, its tests under `tests/`, and `CMakeLists.txt`; the product's header units are the row's other path, `tests/support/headers/`
- **Consumers rely on:** every file of git's listing read against the rules, each module's by its `rules.<M>`, the records' over the tree; a finding names its file and rule; `build`'s configure, the lexer; `tools/loop_modules.py` and `tools/loop_base.py`, `modules`; `tests/drive/test_differential.py`, `content.PATTERNS`
- **Parameters:** none
- **Refuses:** a file that breaks a rule of its own bytes: its test fails; an object, a target or a link edge that breaks a rule over the records: the build fails, naming the file and, where one holds it, the line, as a compiler names an error. `CMakeLists.txt`, off MSVC or with no interpreter: the configure stops. `tools/loop_modules.py`: errors in the table, each one line and status 1; a wrong usage, or a check that cannot run, status 2

The method, once, for the module, which wraps nothing:

- **Compatibility:** Windows built. The records read MSVC's compile commands and `dumpbin`, Windows' alone; the Python itself is `pathlib`, `ast` and `python`'s wraps; no Linux build
- **Extensibility:** a new rule is a row of `lists.py` or `subset.py` and a branch of its checker, an allowance a row with its reason, and a rule with no consumer goes; a new tree is a value of `tools/trees.json`
- **Performance:** the structure preset at 12 jobs, alone, a past reading, measured once at `9d4dd9c` and not since: its 17 tests then in 13.46 s, the slowest 12.64 s; at `0fa40fd` the preset runs 11 tests; a test past 60 s is killed, and a kill under other builds' load is read as load first, by one rerun alone
- **A fake:** none: each rule reads the real checkout and the real build's records, with its planted controls in its tests; the rules' control fails on each allowance row that allowed nothing, by name (`rules.ROWS`, as `build`'s `exemptions.py` does for its list): a row of the file half whose path holds one module's files alone in that module's `rules.<M>`, every other in `tests/test_boundaries.py`, over the rows the build's steps used, which runs `file_rules` over the files of the rows of both halves too; a tool or a check here is proven by use
- **Errors:** a finding of a file's own bytes is a test's failure, with its file and rule; a finding over the records, the build's, `file(line): error rule: what`. A record is written by its object's compile and read by the steps after it: none is stale. `tools/loop_modules.py .` with no build reported 0 errors where the links gave 14 (the kit's rows cut of `text` and `diag`), read at `73b57fc` by (b)'s design challenge, 2026-11-06; at `ee70977`, 2026-11-07, both gave 0 errors, `build/ci-windows` given. The links' half reads the build's codemodel, so a change to a row's edges runs it with `BUILD` given

## Changes

Newest first, each entry its version, its date and the commit it describes.

### 0.6, 2026-11-07, the commit that brings this entry

The Python import check admits `gate`, the test framework's module (its fourth commit), beside `harness`: `tools/gate.py` and `gate`'s own tests import it.

- **This file:** the Holds line of the Python import check.
- **Members changed:** `python_imports.GATE`, new, one of the packages `python_findings` admits.
- **What a consumer does:** nothing.

### 0.5, 2026-11-07, `2d9cbed`

The records and their rules as build steps, by the test framework's design, its third commit (its D-4, records as build steps): the records are the daily tree's, made per object by its compile, and the whole records step goes.

- **This file:** the Holds list names `steps.py` and the steps; the Interface, Refuses, the control and Errors follow.
- **Members changed:** `records.py` makes and reads one object's record (`object_record`, `read_object`), the libraries' names (`library_names`) and the graph (`graph`, given the tree's cache); `Records` holds a set of objects. `rules.Findings.add` takes a line and `errors()` gives `file(line): error rule: what`; `rules.directives` gives each directive's line; `rules.object_rules`, `own_files`, `local_name_rules` and `defined_name_rules` are new, `name_rules`' two parts; `rules.unreached_rules` is new, the directive rules on the files no record of the tree reaches (another platform's sources and headers), which `checks.findings` runs over one module's files given a build with records (`records.tree_records`, `records.objects_by_target`), as the whole step did over every file. `tests/test_boundaries.py` is the control of the rows the steps used. `CMakePresets.json`: `ci-windows` sets `QIVEN_STRUCTURE`, and `ci-asan`, `ci-stack` and `ci-analysis` clear it.
- **Members removed:** the whole step (`records.py`'s `main`, `record`, `record_one`, `digests`, `write_depfile`), the staleness (`Records.stale`, `rules.current_records`), `rules.check`, `record_rules`, `records_directory`, `findings`, `name_rules`.
- **What a consumer does:** builds the tree: a finding fails its build; an existing `build/ci-windows` takes `QIVEN_STRUCTURE` by its preset's configure once (`cmake --preset ci-windows -B build/ci-windows`).
- **Known limits:** the structure tree (`QIVEN_FAULTS` off) is the whole run for the names only it uses. A rule's change reruns every target's step, which applies the object rules again over its own objects, and the link step; an object recompiles only with its sources. A finding over names or links names no line: a symbol or an edge has none. A change elsewhere that makes one of M's files reached or unreached, M's own files unchanged, waits for `rules.<M>`'s next run or the whole run.

### 0.4, 2026-11-07, `eb9faa4`

The checks per module, by the test framework's design, its second commit (its D-4, checks per module): the rules a file's own bytes decide run over one module's files, in `rules.<M>`, so their cost follows the change; the rules over the records stay whole until the records are made per object (the design's third commit).

- **This file:** the Holds list names `checks.py`, `log_includes.py`, the halves and the sets; the Interface, the consumers and the control follow.
- **Members changed:** `rules.check(records, root, files=None)` takes a set; `rules.file_rules`, `record_rules`, `goldens_of`, `half`, `holds`, `row_files`, `FILE_RULES`, `RECORD_RULES` and `current_records` (`findings()`'s records, which it still reads whole) are new. `size.size_findings(root, files)` takes a set. `modules.check` takes `subset`, `whole` and `owner`. `subset.config_rules` reads a file's bytes only for a `.clang-tidy` or `.ruleset`. `tests/test_boundaries.py` runs the records' half (the includes, the directives, the names and the links) and the control of the rows no module's check holds; `tests/test_modules.py` (`FullCheck`) the table, the ownership and the link edges; `tests/test_line_endings.py` `test_as_git_reads` alone.
- **Members moved:** K15, from `tests/test_log_includes.py`'s `findings(root)` to `log_includes.findings(root, listing, files, tree)`.
- **Members removed:** the whole walks, each now a part of every `rules.<M>`: `tests/test_size.py`, `tests/test_content.py`, `tests/test_imports.py`, `tests/test_log_includes.py`, and `test_line_endings.LineEndings.test_tree` with `line_ending_findings`.
- **What a consumer does:** calls `size_findings` with the files; selects a module's per-file rules by `rules.<M>`, which `structure`'s label also selects; reads K15 from `log_includes`. A caller of `rules.check` or `rules.findings` without files gets the whole tree, as before.
- **Known limits:** the directives' rule reads the records and stays with them, by the reviewer's decision for this commit. The structure preset's filter and CI's structure step select by this module's names and run no `rules.<M>` (`CMakePresets.json:179`, `.github/workflows/ci.yml:197`), a known gap the whole runs close. A row of a rule of both halves is the records' test's, its files' own rules run there too.

### 0.3, 2026-11-07, `dd92d92`

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
