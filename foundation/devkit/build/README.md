# build

layer devkit, status built

Version 0.7: the number of this file's change list, below, and of nothing else

- **Awaiting a split:** its product data, which stays here until the product's move (P4): `qiven3-child`, `qiven3_fixtures` and `qiven3_test_headers` in `analysis-exemptions.json`; the product's units' place in `header_units.cmake`; `qiven3-child` in `top.cmake`'s comment on the analysed roots; the product's paths in `tests/test_trees_build.py`'s `test_analysis_choice`, and the product's pair of header units (`qiven3_headers`, `qiven3_test_headers`) in its `test_steps_name_composed`; the product's targets among the five links `tests/test_links.py` must read. The root's product directories and the presets `bench` and `drill` are the composition's, the consumer's, in this row only because it holds those files
- **Holds:**
  - `build.cmake`: `qiven_root`, the checkout's root, from which every repository path of the foundation's CMake starts; the options, pins (MSVC's `19.44.35228`, whose toolset, `14.44.35207`, `tools/gates.json`'s `toolset` names a second time for every tree `tools/gate.py` builds, which no pin checks: a bump changes both; CI unpinned, by `python`'s allowance for `ci.yml`), compiler flags, hardening, sanitizers, clean binaries and analysis
  - `top.cmake`: what the project on top holds, by the same parts in whichever project: the trees, the tidy configuration, the codemodel query, `enable_testing` and the header units' rule at its one include, and `qiven_top_end()`, called last: the foundation's pair of header units for the analysis, `qiven_headers` and `qiven_test_headers`, `binaries.txt`, `executables.txt`, `test-targets.txt`, `defined-targets.txt` and the clang-tidy steps
  - `modules.cmake`: the one list of the foundation's modules, `qiven_foundation_modules`, each added there, at the top project's scope
  - `sanitize.cmake`: the sanitizers of `QIVEN_SANITIZE` on every target, the name of the sanitizer's runtime, and `qiven_fuzz_main`, libFuzzer's main on one fuzz target; `tidy_database.cmake`
  - `trees.cmake`: the trees of `tools/trees.json`, the globs and filters taken from them, and `qiven_kept`, the one filter, narrowing the walks to the `qiven_kept_tree` a project on top names, the table read whole
  - `header_units.cmake`: the header units, their places and names, and the function that makes a pair of them, which `trees.cmake` includes
  - `tidy_config.cmake`: a test source's composed clang-tidy configuration
  - the rule sets `analysis.ruleset` and `stack.ruleset`, the analysis exemptions `analysis-exemptions.json` and the test configuration over them (`analysis-test.ruleset`, `analysis-test.clang-tidy`)
  - two readers its tests use: the link lines of a Ninja build (`links.py`) and the analysis configuration against its list, `analysis-exemptions.json` (`exemptions.py`), whose `findings(root, files, scope=None)` matches the tree's whole configuration, or with `scope` one module's files, both ways, each entry by the files that may turn it off (`sources`)
  - the exemptions' check of one module, `exemptions.<M>` (`checks.py`): for each module that holds a `.clang-tidy` or `.ruleset`, `harness` registers one test, labelled M and `build`, that matches M's files against the list; the list's whole match stays `tests/test_exemptions.py`'s
  - its own Python tests (`tests/`): the trees' globs and filters, `test_trees_build.py`, with its scenes' trees, `tests/scenes/tools/trees.json`; the standard library's hardened mode, `test_hardened.py`; the floors, `test_floors.py`; K11, `test_binaries.py`, each binary of a build held to Windows' system libraries, their API sets (`foundation/devkit/python/system.py`'s `api_set`) or its preset's residual, the lists K11's own there, each binary read by `foundation/devkit/python/imports.py`; each library once on a link line, `test_links.py`; the analysis configuration against its list, `test_exemptions.py`; and the analysis tools' controls, `test_analysis.py`, labelled `analysis`, run on the analysis build alone
  - at the root, `CMakeLists.txt` (the composition: `build.cmake`, `top.cmake`, `modules.cmake`, the product's directories, `qiven_top_end()`), `CMakePresets.json` (the build's and test's presets, which includes `foundation/CMakePresets.json`) and `.clang-tidy`
  - in `foundation/`, `CMakeLists.txt` (the foundation alone, the project with no product directory, by the same parts: its `qiven_kept_tree`, and `qiven_left_out`, the Python tests it leaves out of the listing, each named with its reason) and `CMakePresets.json` (the presets' common part, `base`, `here`, `ci` and the test preset `here`; and `alone`, configure and test, disabled at the root, its tree `build/alone` under the root)
- **Never holds:** C++ source, or Python source outside its `tests/` but the two readers its tests use; another module's target or tests; what makes an executable, diag's `qiven_executable`
- **Contract:** none
- **Interface:** no header: the functions `qiven_top_end` and, in a `QIVEN_SANITIZE` build, `qiven_fuzz_main`, the variables `qiven_root` and `qiven_foundation_modules`, the interface target `qiven_build`, the `QIVEN_` options and cache variables (`QIVEN_PIN_TOOLS`, `QIVEN_STRUCTURE`, `QIVEN_SYSTEM_HEAP`, `QIVEN_FAULTS`, `QIVEN_SANITIZE`, `QIVEN_ANALYZE`, `QIVEN_LOG_MIN`), and the presets; for its tests, the package `foundation.devkit.build`'s `links` and `exemptions`, imported normally; `checks.Exemptions.test_<M>`, the case id of `exemptions.<M>`
- **Files:** the `present` paths of module.json: this directory, its `tests/` among them, the root's three files and `foundation/`'s two
- **Uses:** `structure`'s lexer (`structure/lexer.py`), which `header_units.cmake` runs at configure, and its `subset`, which `exemptions.py` reads, its row's `depends`, and its `layers`, `rules` and `tree`, which `checks.py` reads, with `python`'s `table`; its tests, `python` and `harness` (`run_watched`, a build's directory and options), its `test_depends`, and `structure`'s `layers` and `rules`
- **Consumers rely on:** one set of warnings as errors for every target of ours, through `qiven_build`; the vendored SQLite engine at its own level, warnings not errors (`foundation/system/sqlite/CMakeLists.txt`); the presets' compilers, sanitizers and analysis; clean binaries: no source, toolset or SDK path of the building machine in an object, a library or a binary, and the same bytes from the same sources, for a tree under the root
- **Parameters:** none
- **Refuses:** a build with a warning in a target of ours (warnings are errors). The configure stops, its line beginning `qiven:`, on a tool off its pin under `QIVEN_PIN_TOOLS`, no interpreter named, flags under the language's or the system's floor, a `QIVEN_ANALYZE` or `QIVEN_LOG_MIN` off its values, a sanitizer's runtime not beside the compiler, a toolchain that does not link position-independent executables (off MSVC and Apple), the analysis with no `CppCoreCheckRules.ruleset` under `VSINSTALLDIR`, no interpreter to read the header units' includes or a lexer that cannot read them, a header unit in no module, or a clang-tidy configuration it cannot read

The method, once, for the module, which wraps nothing:

- **Compatibility:** Windows built and pinned, on MSVC and Ninja; the `linux-` and `macos-` presets and `build.cmake`'s branches for them stand on paper here, no Linux build. CI takes its image's tools, unpinned
- **Extensibility:** a new module is a line of `modules.cmake`'s list; a new tree or suffix a value of `tools/trees.json`; a new preset a row of the presets, its test selection beside it. A new `QIVEN_` option needs its consumer in the work at hand
- **Performance:** not measured here: a configure runs the lexer over the header units' sources, and the build's own time is the compiler's
- **A fake:** none: it drives the real tools. Its proof: its own tests, each by label; every build of every preset by use; the alone preset for the foundation's parts, `qiven_left_out` naming what it leaves out; the runner's test of the presets
- **Errors:** each stops the configure with one `qiven:` line naming the value and what it should be, as Refuses above; no error of its own at build time but the compiler's

## Changes

Newest first, each entry its version, its date and the commit it describes.

### 0.7, 2026-11-07, the commit that brings this entry

`tools/changed.py` is retired, `tools/gate.py` gating in its place (the test framework's design, its fourth commit): the toolset the pins name is named a second time in `tools/gates.json`, its `toolset`.

- **This file:** the Holds line of `build.cmake`'s pins; this entry.
- **What a consumer does:** nothing.

### 0.6, 2026-11-07, `eb9faa4`

The exemptions' check per module, by the test framework's design, its second commit (its D-4, checks per module): `exemptions.findings` takes a module's files as its scope, and `checks.py` holds `exemptions.<M>`, which `harness` registers for each module holding a `.clang-tidy` or `.ruleset`, so a change to that module or to this one runs its match.

- **This file:** the Holds list and the Interface name `checks.py` and the scope.
- **What a consumer does:** nothing: the whole match, `exemptions.findings(root, files)`, is as before.
- **Known limits:** an entry is one module's by its possible sources; MSVC's every-file entries may come from any rule set of the list but the test code's, all of them this module's today. A rule set of the list that is not there, two entries and an entry with no reason are the whole match's alone.

### 0.5, 2026-11-07, `560ca16`

The fuzz targets' flag comes here from `tests/fuzz/CMakeLists.txt`, the rest of the end challenges' Bu-4: `qiven_fuzz_main(<target>)`, in `sanitize.cmake`, gives one target libFuzzer's main, as MSVC's compile option or elsewhere's link option. No target's flags change.

- **This file:** the Holds list and the Interface name it; the version line, left at 0.2 by 0.3 and 0.4, follows.
- **What a consumer does:** a fuzz target calls it, in a build whose `QIVEN_SANITIZE` names `fuzzer`; `sanitize.cmake` is included only when `QIVEN_SANITIZE` is set.

### 0.4, 2026-11-07, `4adb15c`

The label `fast` back on the cases 0.2 moved from `structure`, but the analysis controls (2282): the floors, K11, the links and the exemptions, by each class's `ctest_labels`. Each starts no process but git and reads no machine fact, the structure tests' measure (`harness`'s `tests/test_wiring.py`), and each took under a second.

- **What a consumer does:** finds them in the fast preset again, under its 60 s limit; they keep `drive`, `fake` and this module's label.
- **Known limits:** no CI job runs the fast preset.

### 0.3, 2026-11-07, `72c186f`

The scenes' trees, by the end challenges' Bu-7: `tests/scenes/tools/trees.json`, a checkout's trees of the product's shape, which the scenes copy into their scratch checkouts in place of the root's `tools/trees.json`. `test_header_filter` and `test_analysis_choice` judge the rule on it, the latter with the two module directories its answers rest on planted; `test_header_filter` still reads every `.clang-tidy` of the checkout.

- **This file:** the Holds list names the scenes' trees.
- **What a consumer does:** nothing: no member changes.
- **Known limits:** the alone build no longer leaves out `test_header_filter`, `test_analysis_choice` or `harness`'s `test_listing_reader`; `test_one_reader` and `test_steps_name_composed` stay out. A foundation-only clone is not proven by it: `top.cmake` reads the root's `tools/trees.json` (Bu-8, an aim).

### 0.2, 2026-11-07, `f78c42d`

Each case to its subject's row, by the end challenges' St-3 with Bu-6.

- **This file:** the Holds list names the two readers and the cases that came; Never holds, the Interface and Uses follow; the awaiting list names `tests/test_links.py`'s product targets.
- **Members moved:** `links.py` and `exemptions.py` came from `structure`, with the cases of this module's subjects from `structure`'s `tests/`, each whole: the floors (`test_floors.py`), K11 (`test_binaries.py`), the links (`test_links.py`), the analysis controls (`test_analysis.py`, still labelled `analysis`) and the exemptions (`test_exemptions.py`).
- **What a consumer does:** imports `links` and `exemptions` from `foundation.devkit.build`, not `foundation.devkit.structure`; finds the moved cases under `foundation.devkit.build.tests`, selected by this module's label and, but the analysis controls, labelled `drive` as this directory's other tests, not `fast`; the structure preset no longer runs them.
- **Known limits:** the alone build leaves out the moved cases that read what only the whole checkout holds, each named in `foundation/CMakeLists.txt`.

### 0.1, 2026-11-06, `cdf16f1`

The charter in the method's form, by step (b)'s design challenge.

- **This file:** the method stated once, a version and this list; the Holds line one item a file; the options named; the row's edges; the refusals of the configure; clean binaries' promise and its bound.
- **What a consumer does:** nothing: no member changes.
- **Known limits:** clean binaries hold for a tree under the root: `build.cmake` maps `qiven_root` to `.`, and nothing maps a tree elsewhere. A source build at the tag, its tree under its root, is the artifact byte for byte, so a fault gone in it is not a build difference, and a debug package needs no installed sources. The linker maps no path: its own record in each binary's PDB keeps them, a residual. Linux's pins and branches stand unproven. The awaiting list above stands until P4.
