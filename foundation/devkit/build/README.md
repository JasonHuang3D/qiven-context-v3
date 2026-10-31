# build

layer devkit, status built

- **Holds:** `build.cmake` (the options, pins, compiler flags, hardening, sanitizers, clean binaries, analysis and `qiven_executable`), `sanitize.cmake`, `tidy_database.cmake`, the rule sets `analysis.ruleset` and `stack.ruleset`; at the root, `CMakeLists.txt` (the composition, which includes `build.cmake`), `CMakePresets.json` (the sanitizer and analysis presets) and `.clang-tidy`
- **Never holds:** C++ or Python source; a module's own target or tests
- **Contract:** none
- **Interface:** no header: the function `qiven_executable`, the interface target `qiven_build`, the `QIVEN_` options and cache variables, and the presets
- **Files:** the `present` paths of module.json: this directory and the three root files.
- **Consumers rely on:** one set of warnings as errors for every target; the presets' compilers, sanitizers and analysis
- **Parameters:** `constexpr_steps` (not built yet)
- **Refuses:** a build with a warning (warnings are errors)
