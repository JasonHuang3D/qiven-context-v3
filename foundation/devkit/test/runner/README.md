# runner

layer devkit, status built

- **Awaiting a split:** its second row, `support`, holds files that are not the runner's, each the product's move's (P4), to leave with it: the test tree's wiring (`tests/CMakeLists.txt`, which includes the runner's mechanism, `cases.cmake`, and adds the kit's and every module's tests, the product's among them; `tests/unit/CMakeLists.txt`, the product's units), the old test tree's packages while any test lies under `tests/` (`tests/__init__.py`, `tests/drive/__init__.py`; the skip code and a run's end they import are the runner's own, in `skip.py`), and the product's drive (`tests/drive/session.py`).
- **Holds:** the target `qiven_runner`: the cases' registration (`QIVEN_CASE`, `QIVEN_SERIAL_CASE`), the checks (`CHECK`, `CHECK_EQ`, `REQUIRE`, `QIVEN_SKIP`), the allocators a case takes and counts after it, the namers of an error a case lets out, and the run of a binary's cases (`qiven::runner`); the test tree's mechanism (`cases.cmake`): a file's module by the table, a case binary and the registration of its cases (`qiven_add_cases`), and the listing of every test for CTest after the build (`qiven_list_tests`, which runs `ctest_tests.cmake` and `list_tests.py`), waiting for the binaries its caller names; its Python half, which the Python tests import: `skip.py`, a unittest run of skipped tests alone ended with the skip code, and `binaries.py`, a binary of the build run (`QIVEN_BIN`, `QIVEN_BUILD`, `QIVEN_CTEST`), the build's options read and a command run behind the watcher; its own cases and its test through a process (`tests/`)
- **Never holds:** a binary's main, which holds the process's log (the case main, `case_main`); a fault run (`faults`); a module's own tests
- **Contract:** `foundation/devkit/test/runner/include/qiven/runner/runner.hpp`
- **Files:** `include/qiven/runner/` (`runner.hpp`, `core.hpp`), `src/runner.cpp`, `tests/`, `CMakeLists.txt`; `cases.cmake`, which the test tree's CMake file includes, and `ctest_tests.cmake` and `list_tests.py`, which its listing step runs after the build, beside the target they list for; `skip.py` and `binaries.py`; and the `present` paths of module.json outside this directory.
- **Consumers rely on:** a binary's cases listed by name (`--list`) and each run alone by its name (`--run`), its status the one CTest reads (pass 0, fail 1, skip 125); each case's allocators counted after it
- **Parameters:** none
- **Refuses:** two cases of one name, or a name that begins or ends with its dot, and a case or an argument it does not know: usage, status 2; a case that leaves an allocation of its own held: the case fails
- **Words:**
  - `case`: one test body registered by `QIVEN_CASE`, run by name
