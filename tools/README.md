# tools

layer tools, status built

- **Holds:** the developer tooling, in Python: the test runner (`changed.py`), the table's reader
  (`loop_modules.py`, `loop_table.py`, with the old places' base `loop_base.json`, which `loop_base.py`
  writes), git's door (`loop_git.py`), the debuggers (`debug.py`), dumps (`dumps.py`), the watch
  (`watch.py`), status (`status.py`), the fuzz seeds and the counting shim (`loop_shim.py`, `git_shim.*`);
  and the modules' check over the tree (`tests/drive/test_loop_full_check.py`).
- **Never holds:** C++ that a product's program links: tooling sits outside the link graph.
- **Contract:** none
- **Interface:** no header: each tool's `--help` and its one bounded answer.
- **Files:** the `present` paths of module.json, its tests not yet in this directory.
- **Serves:** the developers and models that build and test the repository.
- **Parameters:** none

## Retired

A retired part has left the tree, so that no build, test, check or text keeps it, and stays whole in
git. Each is listed with the last commit that holds it and the one command that restores its files. A
part cut from a file that stays comes back with that file whole as it stood at that commit, the file's
later changes to be made again; the commit after it shows every line cut with the part.

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
- **What only the cut code used, inside kept files:** `loop_run.py` and `own_git.py` down to `git_env`,
  `loop_git.py`'s fake git, `layers.module_file`. Last commit `d2dd025`:
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
