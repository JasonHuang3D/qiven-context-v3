# tools

layer tools, status built

- **Holds:** the developer tooling, in Python: the test runner (`changed.py`), the table's reader
  (`loop_modules.py`, `loop_table.py`, with the old places' base `loop_base.json`, which `loop_base.py`
  writes), git's door (`loop_git.py`), navigation (`nav.py`), the debuggers
  (`debug.py`), dumps (`dumps.py`), the watch (`watch.py`), status (`status.py`), the fuzz seeds and the
  counting shim (`loop_shim.py`, `git_shim.*`); and the modules' check over the tree
  (`tests/drive/test_loop_full_check.py`).
- **Never holds:** C++ that a product's program links: tooling sits outside the link graph.
- **Contract:** none
- **Interface:** no header: each tool's `--help` and its one bounded answer.
- **Files:** the `present` paths of module.json, its tests not yet in this directory.
- **Serves:** the developers and models that build and test the repository.
- **Parameters:** none
