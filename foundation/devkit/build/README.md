# build

layer devkit, status built

- **Awaiting a split:** its product data, which stays here until the product's move (P4): the product's directories in the root `CMakeLists.txt`; `qiven3-child`, `qiven3_fixtures` and `qiven3_test_headers` in `analysis-exemptions.json`; the product's binaries in `sanitize.cmake`'s comment and its units' place in `header_units.cmake`; the presets `bench` and `drill` in `CMakePresets.json`
- **Holds:** `build.cmake` (the options, pins, compiler flags, hardening, sanitizers, clean binaries, analysis and `qiven_executable`), `sanitize.cmake`, `tidy_database.cmake`, `trees.cmake` (the trees of `tools/trees.json` and the globs and filters taken from them), `header_units.cmake` (the header units, their places and names, and the function that makes a pair of them, which `trees.cmake` includes), `tidy_config.cmake` (a test source's composed clang-tidy configuration), the rule sets `analysis.ruleset` and `stack.ruleset`, the analysis exemptions `analysis-exemptions.json` and the test configuration over them (`analysis-test.ruleset`, `analysis-test.clang-tidy`); its own Python tests (`tests/`: the trees' globs and filters, `test_trees_build.py`, every executable made by `qiven_executable`, `test_executables.py`, and the standard library's hardened mode, `test_hardened.py`); at the root, `CMakeLists.txt` (the composition, which includes `build.cmake`, runs the header units' rule and, for the analysis, makes the foundation's pair of them, `qiven_headers` and `qiven_test_headers`), `CMakePresets.json` (the sanitizer and analysis presets) and `.clang-tidy`
- **Never holds:** C++ source or Python source outside its `tests/`; another module's target or tests
- **Contract:** none
- **Interface:** no header: the function `qiven_executable`, which names diag's entry targets though the row's `depends` is empty, the interface target `qiven_build`, the `QIVEN_` options and cache variables, and the presets
- **Files:** the `present` paths of module.json: this directory, its `tests/` among them, and the three root files.
- **Consumers rely on:** one set of warnings as errors for every target; the presets' compilers, sanitizers and analysis
- **Parameters:** none
- **Refuses:** a build with a warning (warnings are errors)
