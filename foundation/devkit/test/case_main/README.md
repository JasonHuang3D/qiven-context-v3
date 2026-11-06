# case_main

layer devkit, status built

Version 0.1: the number of this file's change list, below, and of nothing else

- **Holds:** the target `qiven_case_main`: the main of every case binary: the process's log, each case's `case.end` in it, the name of a platform error a case lets out, over the runner; its writer of `case.end`, `case_ended`, the runner's `Ended` (`qiven/case_main/ended.hpp`); a main's log in the test tree (`qiven/case_main/journal.hpp`, the drives' too); its own cases and its routes through a process, `main` and the runner's, by `qiven_runner_demo`'s cases and `test_runner.py` (`tests/`)
- **Never holds:** the runner's registration or checks (`runner`); a fault run (`faults`)
- **Contract:** none
- **Interface:** no contract header: a case binary links the target and takes its `main`, as `qiven_add_cases` links it; `case_ended` and `Journal` by their headers. A consumer relies on one log a process, its ring in a crash's report and drained at its end, one `case.end` a case, its outcome `pass`, `fail` or `skip` by the case's status, and a `PlatformError` a case lets out named by its code and its detail
- **Files:** `src/main.cpp`, `include/qiven/case_main/` (`ended.hpp`, `journal.hpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::test`, the log's
- **Uses directly:** log (`Log`, `Name`, `Count`, `Millis`, `event`), diag (`keep`, `drop`, `Out`, the journal's ring), memory (`Arena`, `fail_fast`), the platform's contract (`PlatformError`, `name`) and the runner (`run_main`, `registry`, `NamerRegistrar`, `Case`); its row's `text` is the closure of those links, not a use
- **Parameters:** none
- **The method, once:** Windows built, other systems on paper through the platform's contract, no Linux build. A new error's namer is one more `NamerRegistrar`; another event of the main's is one more descriptor beside `case.end`. One log a process and one event a case, not measured. A fake: none, it is the real binaries' main; its proof its own cases, `tests/case_main_test.cpp`, and `tests/test_runner.py` running `runner_demo.cpp`'s cases as processes, each route of a case binary. It refuses nothing of its own: an argument is the runner's to refuse, and an exception out of the run ends the process by memory's `fail_fast`

## Changes

### 0.1, 2026-11-06, the commit that brings this entry

The charter in the method's form, by step (b)'s design challenge: the method stated once, a version and this list; what the module uses directly, its row's `text` being link closure; the Interface line, what a consumer relies on in it. What a consumer does: nothing, no member changing.
