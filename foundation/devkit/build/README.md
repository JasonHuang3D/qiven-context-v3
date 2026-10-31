# build

layer devkit, status built

- **Holds:** the root `CMakeLists.txt` and `CMakePresets.json`, `cmake/`, the compiler flags, the sanitizer and analysis presets, `qiven3_executable`, and the rule sets `.clang-tidy`, `analysis.ruleset`, `stack.ruleset`
- **Never holds:** C++ or Python source; a module's own target or tests
- **Contract:** no header: the CMake functions and presets of its files; not yet in this directory: its files are the `present` paths of module.json.
- **Consumers:** none by the table; every target of the build
- **Consumers rely on:** one set of warnings as errors for every target; the presets' compilers, sanitizers and analysis
- **Consumers give:** `constexpr_steps`
- **Refuses:** a build with a warning (warnings are errors)
