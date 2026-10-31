# runner

layer devkit, status built

- **Awaiting a split:** its second row, `support`, holds files that are not the runner's and leave it: `tests/support/scan/` for `scan` beside it; the test tree's composition (`tests/CMakeLists.txt`, `tests/unit/CMakeLists.txt`), the Python tests' packages (`tests/__init__.py`, `tests/drive/__init__.py`), the drives' session (`tests/drive/binaries.py`, `tests/drive/session.py`) and `tools/status.py`, each for a place not yet named. `tests/drive/test_runner.py`, the runner's test through a process, stays under `tests/drive/` while the Python tests are found there alone.
- **Holds:** the target `qiven_runner`: the cases' registration (`QIVEN_CASE`, `QIVEN_SERIAL_CASE`), the checks (`CHECK`, `CHECK_EQ`, `REQUIRE`, `QIVEN_SKIP`), the allocators a case takes and counts after it, the namers of an error a case lets out, and the run of a binary's cases (`qiven::runner`); the listing of every test for CTest (`ctest_tests.cmake`, `list_tests.py`); its own cases (`tests/`)
- **Never holds:** a binary's main, which holds the process's log (the case main, `case_main`); a fault run (`faults`); a module's own tests
- **Contract:** `foundation/devkit/test/runner/include/qiven/runner/runner.hpp`
- **Files:** `include/qiven/runner/` (`runner.hpp`, `core.hpp`), `src/runner.cpp`, `tests/`, `CMakeLists.txt`, `ctest_tests.cmake` and `list_tests.py`, which the test tree's build runs after it builds, beside the target they list for; and the `present` paths of module.json outside this directory.
- **Consumers rely on:** a binary's cases listed by name (`--list`) and each run alone by its name (`--run`), its status the one CTest reads (pass 0, fail 1, skip 125); each case's allocators counted after it
- **Parameters:** none
- **Refuses:** two cases of one name, or a name that begins or ends with its dot, and a case or an argument it does not know: usage, status 2; a case that leaves an allocation of its own held: the case fails
- **Words:**
  - `case`: one test body registered by `QIVEN_CASE`, run by name
