# diag

layer system, status built

- **Awaiting the analysis:** its private header's unit for the analysis, `tests/support/headers/diag_src_events.cpp` (over `src/events.hpp`), stays under `tests/support/headers/` until the analysis is taken up after the foundation's reconstruction; its place is settled then.
- **Holds:** the diagnostic entry every binary runs before `main`, and the crash report by system; the targets `qiven_diag` (the install, the report, `Out`, a main's output, and the crash ring the report reads, `Ring`) and `qiven_diag_entry` (the entry point and the first initializer, an object every binary takes), with `qiven_diag_entry_wide` for a binary whose entry is `wmain` on Windows; `qiven_diag_targets`, which makes both targets again over another memory or runtime; its own tests (`tests/`): the ring's cases and the report's events, the drive of every binary running the entry (`test_entry.py`), and the entry's planted faults (`tests/faults/`, the program `qiven_diag_fault`, which fills a ring of its own and links the entry alone, with `qiven_diag_fault_crtdbg` on the debug runtime and `qiven_diag_fault_import` over `qiven_diag_fault_lib`) and their drive (`test_faults.py`); the environment names it reads, `QIVEN_LOG` (a main's output), `QIVEN_DUMP_DIR` (the dump's directory, on Windows), `QIVEN_WATCHER` (the watcher's process, on Linux) and `QIVEN_DIAG_PROBE` (the entry's probe), and the definitions it compiles by, `QIVEN_SANITIZED` and `QIVEN_WIDE_MAIN`
- **Never holds:** a product's concept; a window or a dialog
- **Contract:** `foundation/system/diag/include/qiven/diag/diag.hpp`
- **Files:** `include/qiven/diag/` (`diag.hpp`, `out.hpp`, `ring.hpp`), `src/` (`diag.cpp`, `diag_windows.cpp`, `entry.cpp`, `events.hpp`, `out.cpp`, `ring.cpp`, and Linux's `diag_linux.cpp`, `signal.cpp` and `signal.hpp`, moved as they are and not built on Windows), `tests/`, `CMakeLists.txt`; their namespace `qiven::diag`, with `qiven::diag::detail`
- **Consumers rely on:** a fault ends the process with one bounded report on standard error: its cause, stack, last events, dump and status
- **Parameters:** none
- **Refuses:** a window on a fault: none is shown
