# harness

layer devkit, status built

Version 0.4: the number of this file's change list, below, and of nothing else

- **Holds:** the runner's tool half, meeting its C++ kit (`runner`) by process alone: `cases.cmake`, a case binary (`qiven_add_cases`) and the listing step (`qiven_list_tests`); `wiring.cmake`, the foundation's test directories and their end; `ctest_tests.cmake` and `list_tests.py`, each test listed behind the watcher; the watcher, `watch.py`; Python's case entry, `case.py`; the dumps' bound, `dumps.py`; `binaries.py`, the build's binaries and options and a watched run
- **Never holds:** C++; a case's registration or checks (`runner`); a binary's main (`case_main`); a fault run (`faults`)
- **Contract:** none
- **Interface:** `qiven_add_cases`, `qiven_list_tests`, `qiven_foundation_tests_end`; the package `foundation.devkit.test.harness`: `binaries`; the watcher's and the case entry's command lines
- **Files:** the eight above; `tests/`, their tests; `module.json`. `tests/CMakeLists.txt` and `foundation/CMakeLists.txt` include `wiring.cmake`
- **Uses directly:** `python`'s `answer`, `build_tree`, `debugger`, `imports`, `process`, `status`, `system`, `table`; its tests, diag's `qiven_diag_fault` and the runner's cases
- **Consumers rely on:** every test behind the watcher, its status in one last line, its exit the case's, past its limit a hang, 124; a test labelled with its module's name, `tools/changed.py`'s key; a skip its exit, 125, in both languages, read by `SKIP_RETURN_CODE`, its reason printed; a watched run by `run_watched` alone
- **Parameters:** none
- **Refuses:** the listing, failing the build: a module not imported, a test of no module or labels, a test directory with no `__init__.py`, `--leave-out <name>: no test of that id or package`. The watcher, status 2: no `--limit`, `--name` or command; `watch.py: <test>: the case cannot start: <reason>`. The case entry, status 2: no `--skip` or id
- **The watcher:** `python -I -X faulthandler foundation/devkit/test/harness/watch.py --limit <s> --name <test> -- <command>...` runs the case, sets `QIVEN_WATCHER`, `QIVEN_DUMP_DIR` and `QIVEN_LOG`, names the status by `status` and a library missing at load by `imports`, and bounds `dumps/`. Past `--limit`: `debugger.hang_stacks`, at most 16 threads and 200 lines, and `<binary>-<pid>-hang.dmp`, or `hang: no debugger, no stacks`; the tree ended, status 124. Its worst case against CTest's 30 s (`AFTER_LIMIT`) is in `debugger`'s charter: `HANG_SECONDS` 12 s, `READER_WAIT` 5 s
- **The dumps' bound:** `bound(directory, pid=None, kept=DUMPS_KEPT)`, 8: the newest `kept` `*.dmp` and every dump of `pid` stay, a line for each removed or not; `hang_dump(pid, binary, directory)`, the hang dump's one name
- **`run_watched(command, name, env=None, cwd=None, limit=TIME_BOUND)`:** behind the watcher, `TIME_BOUND` 10 s, polled every `POLL`, 0.1 s, for windows, its tree ended past the limit and twice `AFTER`, 30 s: `(status, out, err, seconds, seen)`, the dumps' lines dropped
- **Python's case entry:** `python -I foundation/devkit/test/harness/case.py --skip <code> <test id>`, from the root, which it appends to `sys.path`, runs that one test by unittest's loader by name, unittest's lines on standard error with each skip's reason: exit 1 for a failed run; else `<code>`, the suite's (`QIVEN_SKIP_CODE`, 125), on a skip, each named `SKIP <id>: <reason>`; else 1 where no test ran, `no test ran: <id>`; else 0. A test that skipped a subtest and passed the rest is a skip. Its standard library alone
- **The method, once:** Windows built; POSIX and macOS on paper, through `python`. A new kind of test is one more `qiven_add_test`. The watcher's start, measured once before `1612fde`: about 90 ms a test, ours 27.6 ms. A fake: none, the watcher fronts the real case. Proof: `test_hang` (`Hang` with a stand-in cdb, `fake`; K8, `debugger`), `test_wiring` (each test's command, labels, limit, skip; the presets; one id by the case entry), `test_listing` (the listing, CTest's start of a listed test through the case entry; `fake`), the dumps' bound by use

## Changes

Newest first, each entry its version, its date and the commit it describes.

### 0.4, 2026-11-07, the commit that brings this entry

Python's case entry, by the test framework's design, its first commit (E-22, E-32, 2258): `case.py` runs one test by its id and exits 125 on a skip, its reason printed; every test's skip is `SKIP_RETURN_CODE`; the regular expression on unittest's sentence and the watcher's status line, and its limits (a pass printing that sentence read as a skip, a line between them making a skip a pass), are retired. `binaries.py`'s `CASE` and `case_run` replace `unittest_run`; `test_wiring` and `test_listing` read the new command.

- **What a consumer does:** nothing in a test: a Python test skips by unittest's skip, as before. A caller of `binaries.unittest_run` calls `case_run(test_id)`; none is left.
- **Known limits:** the entry loads a test by name, where discover imported every module of its top package first; no test module skips at its import or has `load_tests`, the two ways the two differ. The root is appended where discover put it first: a site package named as a top package (`tests`, `tools`) would win; the pinned interpreter holds pip alone.

### 0.3, 2026-11-07, `72c186f`

The scenes' trees, by the end challenges' Bu-7, `build`'s: `test_listing_reader` reads its planted paths' module directories (`tools`, `foundation/core/memory`) beside the checkout's, its assertions as they were.

- **What a consumer does:** nothing: no member changes.
- **Known limits:** the alone build runs `test_listing_reader`, no longer left out. Its walk reads the checkout's trees and table, as before.

### 0.2, 2026-11-07, `fa3c767`

The listing's cases to the listing's row, by the end challenges' Bu-6: `test_discovery`, `test_ctest_start` and `test_listing_reader` moved whole from `build`'s `tests/test_trees_build.py` into `tests/test_listing.py`, class `Listing`, labelled `fake` as before; the helper they need (`succeeded`, `Stopped`) and the trees' table they read (`TREES`, `HOMES`) copied with them, `build`'s test keeping its own.

- **What a consumer does:** nothing: no member changes. `build`'s tests no longer import `list_tests`; a change to the listing selects its cases by this module's label.
- **Known limits:** the alone build leaves `test_listing_reader` out by its new id, as it left it out before.

### 0.1, 2026-11-07, `e972112`

The runner's tool half made a module of its own, by the owner's yes to step (b)'s challenge's Te-6: `cases.cmake`, `wiring.cmake`, `ctest_tests.cmake`, `list_tests.py`, `watch.py`, `dumps.py`, `binaries.py` and their tests, `tests/test_hang.py` and `tests/test_wiring.py`, moved from `foundation/devkit/test/runner/`, unchanged but for their own paths and the listing's placeholder label (`qiven.not_built`, `harness`); this charter made of the runner's sections on them, shortened to its bound.

- **What a consumer does:** imports `foundation.devkit.test.harness` (`binaries`, `list_tests`) and includes `foundation/devkit/test/harness/wiring.cmake`; a row whose tests run it says `harness` in its `test_depends` (`build`'s).
- **Known limits:** `tools/changed.py` selects a changed module's tests by its label alone: a change here runs `test_hang` and `test_wiring`, no longer the runner's cases, which a change to these files ran before. The runner's longer words on the watcher, the dumps' bound and the skip stand in its 0.1 text (`git show 051e1d4:foundation/devkit/test/runner/README.md`) and in `watch.py`'s docstring.
