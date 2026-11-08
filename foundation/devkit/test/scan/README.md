# scan

layer devkit, status built

Version 0.1: the number of this file's change list, below, and of nothing else

- **Holds:** the target `qiven_scan`: the raw-byte scans over a directory's files, on the standard library alone: each file read whole, each hit of a named byte pattern with its file and offset, a file it cannot read a hit of its own; its own cases (`tests/`), the patterns' bytes and a directory scanned in a scratch directory of their own
- **Never holds:** a scan of another module's files, that module's test (a store's directory scanned is the store's, `foundation/services/storage/recordlog/tests/scan_test.cpp`)
- **Contract:** `foundation/devkit/test/scan/include/qiven/scan/scan.hpp`
- **Interface:** `utf16le`, `marker`, `scan`, and its answer, `Report` (`files`, `hits`, `lists`). A consumer relies on every regular file under the directory read, its subdirectories included, in path order, so that nothing passes unscanned
- **Files:** `src/scan.cpp`, `include/qiven/scan/scan.hpp`, `tests/scan_test.cpp`, `CMakeLists.txt`; their namespace `qiven::test::scan`
- **Uses directly:** nothing of the foundation: the standard library alone. Its row's `test_depends` are its own cases' link closure, through `case_main`'s main
- **Parameters:** none
- **The method, once:** the standard library's files, the same on every system, Windows built, no Linux build. Another pattern is a value its caller gives, no code here. Each file's bytes in a string, test side, not measured. A fake: none, a scratch directory being its input; its proof its own cases and the record log's scan. Its errors: a `marker` or `utf16le` of invalid UTF-8, `std::invalid_argument`; a directory or a file it cannot read, a hit named `unread`, never an error

## Changes

### 0.1, 2026-11-06, the commit that brings this entry

The charter in the method's form, by step (b)'s design challenge: the method stated once, a version and this list; the Interface line; what the module uses directly, nothing above it, its `test_depends` being link closure. What a consumer does: nothing, no member changing.
