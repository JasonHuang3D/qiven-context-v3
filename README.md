# repository

layer repository, status built

- **Contract:** none
- **Parameters:** none
- **Holds:** the root's notes: this README, the licence, `.gitattributes`, `.gitignore` and `.github/`, and the module table's root file; the build's root files are devkit's `build` module's, and every other file is its module's

Qiven Context v3 (`qiven-context-v3`): a native C++20 core that keeps a person's working context
as entries in a local SQLite store, at a directory its user chooses; selects
what a session reads from it; forgets on request; and seals the store's
history into self-verifying bundles, which a separate reference reader
checks.

Status: under construction. Nothing here is released.

## Specification

The format is specified in `docs/FORMAT.md`, a self-contained document that
arrives with the code. Until then, this repository holds no specification.

## Building and running

CMake and Ninja, C++20: `cmake --preset <name>` configures out of the tree
into `build/<name>/`, `cmake --build --preset <name>` builds and
`ctest --preset <name>` tests. Nothing is installed.

- Developer presets, on Windows with MSVC: `debug`, `release` (the build the
  full suite runs on), `asan`, `fuzz`, `structure` (the structure records),
  `stack` (the frame bound), `analysis` (static analysis, then clang-tidy)
  and `nohal` (no HAL).
- CI's presets, which share `build/ci`: `ci-windows`, `ci-asan`, `ci-stack`
  and `ci-analysis` on Windows with MSVC; `linux-gcc` and `linux-clang`
  (AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer),
  `linux-tsan` (ThreadSanitizer) and `linux-fuzz` (libFuzzer) on Linux. The
  `macos-clang` preset and its floor are kept with no job to run them;
  restoring the one job block brings it back. `.github/workflows/ci.yml`
  runs them by manual dispatch
  alone, never on a pull request, a tag or a push to a branch, and a later
  dispatch never cancels a run in progress. CI is a runtime evaluation, unpinned:
  Linux on the `ubuntu-24.04` image, Windows on the runners'
  latest images, each with its own tools, CPython's current release by
  `actions/setup-python`, each job printing its image and its tools'
  versions; the pins stay in the developer presets.
- Test presets select by label: `full`, `fast`, `structure`, `asan`,
  `fuzz`, `bench`, `drill`, `analysis` and `debugger` (the cases that drive
  a debugger, never CI's); and, for CI, `fake` (the cases on the fakes) and
  `ci-analysis`. Configure and build the `structure` preset before
  `ctest --preset full`: the structure tests it runs read that build's
  records.
- The foundation alone, with no product directory, and those of its tests
  that need no product: `cmake -S foundation --preset alone`, then
  `cmake --build build/alone` and
  `ctest --test-dir build/alone --no-tests=error -j 12`, or
  `ctest --preset alone` from `foundation/`. The preset `alone` and the
  presets' common part are in `foundation/CMakePresets.json`, which the
  root's file includes.
- The developer presets pin the toolchain (`QIVEN_PIN_TOOLS`): configure
  fails on any MSVC but 19.44.35228, any Windows SDK but 10.0.26100.0, any
  CMake but 4.4.3, any Ninja but 1.13.2, or any interpreter but CPython
  3.15.0. Run them from a shell that `vcvars64.bat -vcvars_ver=14.44.35207`
  has set up. CI's presets leave the option off.
- Name the interpreter by the environment variable `QIVEN_PYTHON`, which
  comes first, or in an untracked `CMakeUserPresets.json` by a preset that
  inherits one of these and sets the cache variable `Python3_EXECUTABLE`. A
  configure with neither keeps the interpreter its tree's cache holds, and
  a new tree's fails under the pins. No machine path is ever committed.
- Name the debugger by the environment variable `QIVEN_CDB`, a `cdb.exe` of
  the version and SHA-256 that `foundation/devkit/python/debugger_pins.txt`
  pins. Without it the Windows SDK's is read, which is off the pin, so that
  the debugger's cases fail, naming the variable, and never skip.

## What this repository never holds

- Anyone's cognition: a live store stays at the location its user chooses,
  and a backup goes to a private repository.
- Local evidence, or a path of any machine.
- A real secret: the secret check's fixtures use documented example values,
  or build their strings when a test runs.

## License

Apache License 2.0; see `LICENSE`.
