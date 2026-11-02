# scan

layer devkit, status built

- **Holds:** the target `qiven_scan`: the raw-byte scans over a directory's files, on the standard library alone: each file read whole, each hit of a named byte pattern with its file and offset, a file it cannot read a hit of its own; its own cases (`tests/`), the patterns' bytes and a directory scanned in a scratch directory of their own
- **Never holds:** a scan of another module's files, that module's test (a store's directory scanned is the store's, `tests/unit/scan_test.cpp`)
- **Contract:** `foundation/devkit/test/scan/include/qiven/scan/scan.hpp`
- **Files:** `src/scan.cpp`, `include/qiven/scan/scan.hpp`, `tests/scan_test.cpp`, `CMakeLists.txt`; their namespace `qiven::test::scan`
- **Parameters:** none
