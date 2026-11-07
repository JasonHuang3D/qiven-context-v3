# tools

layer tools, status built

- **Holds:** front ends alone, the developer tooling in Python above `foundation/devkit/`: the test gate's
  command line (`gate.py`, its method the module `foundation/devkit/test/gate/`) and the gates it runs, as
  data (`gates.json`: each gate's steps, a configure preset, its selection by key or whole, its labels or
  name pattern, its repeat and its reach; the gate that owns each configuration; the toolset), the modules'
  check's command line (`loop_modules.py`; the check is
  `foundation/devkit/structure/modules.py`, the table's reader `foundation/devkit/python/table.py`), the
  old places' base `loop_base.json`, which `loop_base.py` writes, the debugger agents drive (`debug.py`, cdb
  found, checked and driven by `foundation/devkit/python/debugger.py`) and the fuzz seeds (`fuzz_seeds.py`);
  the repository's data read at the root, the trees the reader reads (`trees.json`); and its tests in
  `tests/`: the debugger's (`test_debug.py`) and the modules' check by its command line
  (`test_loop_modules.py`, a test of the tree in place of a hand step). The debugger's pin is `debugger.py`'s, beside it
  (`foundation/devkit/python/debugger_pins.txt`, moved from `windows-pins.txt` here at `1612fde`).
  The watcher, its dumps' bound and the watcher's hang path's tests are `harness`'
  (`foundation/devkit/test/harness/`), the runner's tool half.
- **Never holds:** C++ that a product's program links. A dependency on what is not ours (an operating
  system, a third-party library, an SDK) is ours to own: one foundation module wraps it, with its headers
  private to that module or vendored; no foreign header reaches a header of ours; all other code calls only
  its own lower layers and the standard library (`DIRECTION-3.md`). The named departures are in
  `foundation/README.md`.
- **Contract:** none
- **Interface:** no header: each tool's `--help` and its one bounded answer.
- **Files:** the `present` paths of module.json, its tests in `tests/`.
- **Serves:** the developers and models that build and test the repository; devkit, by the edges
  `foundation/devkit/README.md` names.
- **Parameters:** none

## Retired

A retired part has left the tree, so that no build, test, check or text keeps it, and stays whole in
git. Each is listed with the last commit that holds it and the one command that restores its files. A
part cut from a file that stays comes back with that file whole as it stood at that commit, the file's
later changes to be made again; the commit after it shows every line cut with the part.

- **The test runner `changed.py`:** the runner by a record of each module's files
  (`build/ci-windows/changed.json`), replaced by `gate.py`, by the test framework's design, which gates
  every commit from the commit that retires it; it had no tests of its own. With it, inside a file then
  kept, what only it used: `build_tree.run_tests` with its `Results` and `_RESULT`
  (`foundation/devkit/python/build_tree.py`). A restore brings back a second tool that gates nothing and
  the toolset named a third time, and `build_tree.py` without the same commit's `run_ctest` and
  `run_build`'s `source` and `targets`, which `gate`'s `run.py` calls: made again in the file it brings
  back. Last commit `2d9cbed`:
  `git restore --source=2d9cbed -- tools/changed.py tools/module.json foundation/devkit/python/build_tree.py`

- **The loop with its parts:** the edit loop and the parts only it used, eight files: `loop.py`,
  `loop_fuzz`, `loop_jobs`, `loop_linux`, `loop_select`, `loop_tiers`, `loop_windows` and `linux_pins`.
  Last commit `d2dd025`:
  `git restore --source=d2dd025 -- tools/loop.py tools/loop_fuzz.py tools/loop_jobs.py tools/loop_linux.py tools/loop_select.py tools/loop_tiers.py tools/loop_windows.py tools/linux_pins.py`
- **The tooling row's tests:** 20 test files and the helpers `fake_git.py`, `pinned.py` and
  `fixtures/nav_publish.cpp`, all in `tests/drive/`. Last commit `d2dd025`:
  `git restore --source=d2dd025 -- tests/drive/fake_git.py tests/drive/pinned.py tests/drive/fixtures/nav_publish.cpp tests/drive/test_dumps.py tests/drive/test_git_count.py tests/drive/test_git_shim.py tests/drive/test_linux_pins.py tests/drive/test_loop.py tests/drive/test_loop_answer.py tests/drive/test_loop_base.py tests/drive/test_loop_ci.py tests/drive/test_loop_git.py tests/drive/test_loop_layers.py tests/drive/test_loop_modules.py tests/drive/test_loop_pins.py tests/drive/test_loop_rows.py tests/drive/test_loop_select.py tests/drive/test_loop_table.py tests/drive/test_loop_tiers.py tests/drive/test_loop_trees.py tests/drive/test_nav.py tests/drive/test_tier_one.py tests/drive/test_watch.py`
- **Structure's tests of its rules and plants:** the Plant classes, the planted and synthetic cases of
  the kept files, `test_size_rule.py`, `test_suffixes.py`, and the planted project nothing reads after
  them (`plants.py`, `records.py`'s plant step, `rules.py`'s plant branch, its `DEPENDS`). Last commit
  `d2dd025`:
  `git restore --source=d2dd025 -- foundation/devkit/structure`
- **What only the cut code used, inside files then kept:** `loop_run.py` and `own_git.py` down to `git_env`,
  `loop_git.py`'s fake git, `layers.module_file`. What was left of `loop_run.py` and `loop_git.py` left the
  tree at `b76f0e4`, not retired but replaced by `foundation/devkit/python/process.py` and `git.py`: the
  command brings the two back as files nothing reads until the cut code they served is back. Last commit
  `d2dd025`:
  `git restore --source=d2dd025 -- tools/loop_run.py tests/drive/own_git.py tools/loop_git.py foundation/devkit/structure/layers.py`
- **The nav tools:** `nav.py`, `nav_lsp.py` and `nav_query.py`, the C++ navigation aid over clangd, run by
  hand. Last commit `597b22f`:
  `git restore --source=597b22f -- tools/nav.py tools/nav_lsp.py tools/nav_query.py`
- **Two drive parts whose job left (1674):** the `cwd` echo the fake drive's `set_programs` accepted
  (`tests/driver/instances_fake.cpp`), and the Windows drive's `publish` hold, a reader on a staging
  directory's first file with the stage record it read (`tests/machine/windows_holds.py`), each used by
  nothing once the drive's cases that sent them became platform's own. The `cwd` field of the drive's
  `start` and `run` stays. Last commit `5c19e8a`:
  `git restore --source=5c19e8a -- tests/driver/instances_fake.cpp tests/machine/windows_holds.py`
- **The drive's parts used by nothing (1689):** a path's `path_hex` form (`given_path`,
  `tests/driver/platform_files.hpp`); the drive's `lock` and `lock_release` requests and their held Locks
  (`platform_files.hpp`, `platform_ops.hpp`, `platform_ops_impl.hpp`); the fake drive's `set_busy`
  (`tests/driver/instances_fake.cpp`); the fake drive's `--seed` option (`tests/driver/common.cpp`,
  `drive.hpp`, `instances_fake.cpp`, `store_ops.cpp`, whose Site's seed is now 0), with the bindings'
  `seed` parameter (`tests/contract/platform_cases.py`, `test_platform_fake.py`, `store_cases.py`,
  `tests/machine/test_platform_windows.py`); `Drive.random_bytes` and `Drive.now`
  (`tests/drive/session.py`); and the Windows holders whole, the bindings' `hold` and `held` with
  `tests/machine/windows_holds.py`'s `HeldDrive` and `Holder`, each used by nothing once the drive's
  cases that sent them became platform's own. The drive's `random_bytes` and `now` requests stay. Last
  commit `90d05e4`:
  `git restore --source=90d05e4 -- tests/driver/platform_files.hpp tests/driver/platform_ops.hpp tests/driver/platform_ops_impl.hpp tests/driver/common.cpp tests/driver/drive.hpp tests/driver/instances_fake.cpp tests/driver/store_ops.cpp tests/driver/store_ops.hpp tests/drive/session.py tests/contract/platform_cases.py tests/contract/platform_files.py tests/contract/test_platform_fake.py tests/contract/store_cases.py tests/machine/test_platform_windows.py tests/machine/windows_holds.py products/qiven3/tests/module.json`
- **Two drive requests used by nothing (1707):** the fake drive's `set_found` (`tests/driver/instances_fake.cpp`)
  and the drive's `sink_close` (`tests/driver/platform_files.hpp`, `platform_ops.hpp`,
  `platform_ops_impl.hpp`), with their operation names (`tests/driver/common.cpp`), each sent by no case. Last commit `19eb790`:
  `git restore --source=19eb790 -- tests/driver/instances_fake.cpp tests/driver/platform_files.hpp tests/driver/platform_ops.hpp tests/driver/platform_ops_impl.hpp tests/driver/common.cpp`
- **The fake platform's `set_found`, used by nothing (1723):** `FakePlatform::set_found`, the setter of the paths
  `program(name)` gives, called by no code once the drive's request that sent it left; `Config::found` stays, the
  fake's own. Last commit `5756392`:
  `git restore --source=5756392 -- foundation/system/platform/include/qiven/platform/fake/fake_platform.hpp`
- **The Linux-only Python:** gdb's wrapper `debug_gdb.py`, `linux-pins.txt`, and K19's `hardening.py` with
  its test, whole; inside kept files, `imports.py`'s ELF and glibc parts with K11's Linux half, `debuggers.py`'s
  gdb, `debug.py`'s gdb path, `watch.py`'s symbolizing and Linux loader path with `QIVEN_ADDR2LINE`,
  `dumps.py`'s core files, the build's `QIVEN_CXX_COMPILER_ID`, and the Linux arms and cases of the tests that
  read them. The workflows and all C++ Linux code stay. Its Linux jobs were last green in CI at `fa3852f`,
  2026-10-30. Of the files then kept, `debuggers.py` has since left the tree, its cdb now
  `foundation/devkit/python/debugger.py` (`73d4148`), and `watch.py`, `dumps.py` and `tests/test_hang.py`
  moved to `foundation/devkit/test/runner/` (`6161164`): the command brings them back at their old places. A
  restore needs `status`'s, `system`'s, `process`'s, `git`'s, `table`'s, `build_tree`'s, `answer`'s and
  `debugger`'s moves and the watcher's move to the runner made again in the files it brings back, and Linux
  returns through `system`'s POSIX back end, not by this command alone. Since then the imports reader has
  moved to `foundation/devkit/python/imports.py`, K11's lists into `structure`'s `tests/test_binaries.py`
  (`68734ca`), and the debugger's pin from `tools/windows-pins.txt` to
  `foundation/devkit/python/debugger_pins.txt` (`1612fde`): a restore makes these two moves again too, in
  the files it brings back, which name the old places. Last commit `e0cb5c4`:
  `git restore --source=e0cb5c4 -- tools/debug_gdb.py tools/linux-pins.txt tools/debug.py tools/debuggers.py tools/dumps.py tools/watch.py tools/module.json tools/tests/test_debug.py tools/tests/test_hang.py foundation/devkit/structure/hardening.py foundation/devkit/structure/imports.py foundation/devkit/structure/README.md foundation/devkit/structure/tests/test_hardening.py foundation/devkit/structure/tests/test_binaries.py foundation/devkit/build/build.cmake foundation/devkit/test/runner/cases.cmake foundation/devkit/test/runner/ctest_tests.cmake foundation/system/diag/tests/test_faults.py`
