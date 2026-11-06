# case_main

layer devkit, status built

- **Holds:** the target `qiven_case_main`: the main of every case binary: the process's log, each case's `case.end` in it, the name of a platform error a case lets out, over the runner; its writer of `case.end`, the main's `Ended` (`qiven/case_main/ended.hpp`); a main's log in the test tree (`qiven/case_main/journal.hpp`, the drives' too); its own cases (`tests/`)
- **Never holds:** the runner's registration or checks (`runner`); a fault run (`faults`)
- **Contract:** none
- **Files:** `src/main.cpp`, `include/qiven/case_main/` (`ended.hpp`, `journal.hpp`), `tests/`, `CMakeLists.txt`; their namespace `qiven::test`, the log's
- **Parameters:** none
