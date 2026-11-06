# faults

layer devkit, status built

Version 0.1: the number of this file's change list, below, and of nothing else

- **Holds:** the target `qiven_faults`: fault injection's loop, a header over memory's countdown writing each failure it injects to the case's log (`qiven/faults/faults.hpp`), a `fault run` being one run of an operation with its k-th allocation or platform call failed, and a fault run's parts (`qiven/faults/parts.hpp`); its own case, `faults.counts` (`tests/faults_test.cpp`): each count `run_faults` gives and a pass run in parts
- **Never holds:** a module's fault seam (memory's failing countdown, the platform's failing calls), that module's own code; a test of one module's behaviour
- **Contract:** `foundation/devkit/test/faults/include/qiven/faults/faults.hpp`
- **Interface:** `run_faults` and its answer, `FaultRuns`, and the event `fault.injected`; `parts.hpp`'s `FaultParts` and `PartRuns`. An interface library, headers alone; built without `QIVEN_FAULTS` the loop is not there, and a case says so as not applicable
- **Files:** `include/qiven/faults/` (`faults.hpp`, `parts.hpp`), `tests/` (its case and its headers' units), `CMakeLists.txt`; their namespace `qiven::test`
- **Uses directly:** memory (`Countdown`), log (`Log`, `Name`, `Count`, `Types`, `Level`, `event`) and the runner (`registry`, `Case`), `parts.hpp` registering a fault run's cases in its registry
- **Consumers rely on:** an operation run with its k-th allocation or platform call failed, for k from 1 until it completes; each case's log
- **Parameters:** none
- **Refuses:** nothing itself: `run_faults` counts a fault run that does not end in the operation's typed error (`untyped`) or whose check fails (`unchecked`), and a case fails only by checking those counts
- **Compatibility, extensibility and performance:** no system call and no conditional on a system: the countdown is memory's, and a platform's failing call the fake's, Windows built, no Linux build. A new fault point is its module's seam, no code here; a new operation one call of `run_faults`. One thread, a run for each k up to `limit`, 100000 by default, each part of a fault run within 30 s on every job; not measured here
- **A fake and errors:** none, the module's countdown standing in; its proof `faults.counts`, skipped in a build without `QIVEN_FAULTS`, and every module's fault cases by use. It has no error of its own: an operation's error is what it counts, and a part's case dropped is refused by `registered()`

## Changes

### 0.1, 2026-11-06, the commit that brings this entry

The charter in the method's form, by step (b)'s design challenge: the method stated once, a version and this list; the Interface line; what the module uses directly. What a consumer does: nothing, no member changing.
