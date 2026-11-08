# runner

layer devkit, status built

Version 0.2: the number of this file's change list, below, and of nothing else

- **Awaiting a split:** its second row, `support`, holds files that are not the runner's, each the product's move's (P4), to leave with it: the product's test wiring (`tests/CMakeLists.txt`, which includes the foundation's, `harness`' `wiring.cmake`, adds the product's test directories and then calls the foundation's end, `qiven_foundation_tests_end`; `tests/unit/CMakeLists.txt`, the product's units), the old test tree's packages while any test lies under `tests/` (`tests/__init__.py`, `tests/drive/__init__.py`), and the product's drive (`tests/drive/session.py`)
- **Holds:** the target `qiven_runner`: the cases' registration (`QIVEN_CASE`, a `case` being one test body it registers, run by name), the checks (`CHECK`, `CHECK_EQ`, `REQUIRE`, `QIVEN_SKIP`), the allocators a case takes and counts after it, the namers of an error a case lets out, and the run of a binary's cases (`qiven::runner`); its own cases (`tests/`). Its tool half, which meets it by process alone (`--list`, `--run`, statuses), is `harness`: the test tree's mechanism and wiring, the listing, the watcher, the dumps' bound and the Python tests' binaries
- **Never holds:** a binary's main, which holds the process's log (the case main, `case_main`); a fault run (`faults`); the test tool (`harness`); a module's own tests
- **Contract:** `foundation/devkit/test/runner/include/qiven/runner/runner.hpp`
- **Interface:** the header `qiven/runner/runner.hpp` over `core.hpp`; a case binary's command line, `--list` and `--run <case>`
- **Files:** `include/qiven/runner/` (`runner.hpp`, `core.hpp`), `src/runner.cpp`, `tests/` (`runner_test.cpp`, `support/`), `CMakeLists.txt`; and the `present` paths of module.json outside this directory
- **Uses directly:** memory (`Arena`, `Pool`, `Heap`, `MemoryError`, `fail_fast`, `add`, `name`) and log (`Name` in `QIVEN_CASE`, a `Log&` in a case's context). Its row's `text` and `diag` are the closure of those links, not a use; its tests take `case_main`'s main and the platform
- **Consumers rely on:** a binary's cases listed by name (`--list`) and each run alone by its name (`--run`), its status the one CTest reads (pass 0, fail 1, skip 125, `SKIP_CODE` in `harness`' `binaries.py`); each case's allocators counted after it
- **Parameters:** none
- **Refuses:** two cases of one name, or a name that begins or ends with its dot, and a case or an argument it does not know: usage, status 2; a case that leaves an allocation of its own held: the case fails
- **Compatibility, extensibility and performance:** Windows built, no Linux build. A new check is one more macro over `check`
- **A fake and errors:** none: its cases are the real registration and run. Its proof: its own cases, `tests/runner_test.cpp`, and every case binary by use

## Changes

### 0.2, 2026-11-07, `e972112`

The tool half moved to its own module, `harness` (`foundation/devkit/test/harness/`), by the owner's yes to step (b)'s challenge's Te-6: `cases.cmake`, `wiring.cmake`, `ctest_tests.cmake`, `list_tests.py`, `watch.py`, `dumps.py`, `binaries.py`, `tests/test_hang.py`, `tests/test_wiring.py` and `tests/__init__.py`. This file loses their lines (the Holds and Files clauses, the Interface's CMake functions and Python module, the Python half among its uses, the listing's and the watcher's refusals, the sections on the watcher, the dumps' bound, `run_watched` and a Python test's skip, and their compatibility, performance and proofs); the rest stands, not cut to its bound.

- **What a consumer does:** its import of `binaries` or `list_tests`, and its `include()` of `wiring.cmake`, names `foundation.devkit.test.harness` or `foundation/devkit/test/harness/`; a row whose tests run them says `harness` in its `test_depends` (`build`'s). A consumer of the C++ kit does nothing.

### 0.1, 2026-11-06, `cdf16f1`

The charter in the method's form, by step (b)'s design challenge: the method stated once, a version and this list; what the module uses directly, its row's `text` and `diag` being link closure; the watcher's, the dumps' bound's, `run_watched`'s and the skip's sections each one line, their words kept; the Interface line and the skip's limit. What a consumer does: nothing, no member changing.
