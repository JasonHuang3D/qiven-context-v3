# platform

layer system, status built

- **Awaiting a split:** its product half leaves for `products/qiven3`: `facts` (BitLocker, file history and the volume's protection), `synced` (the sync roots) and `residual` (the residual rows), with their types `State`, `Reading`, `Facts` and `Row`, today in the contract (`include/qiven/platform/platform.hpp`), the Windows adapter (`include/qiven/platform/windows/windows.hpp`; `src/windows/facts.cpp`, `facts.hpp`, `facts_calls.cpp`, `facts_calls.hpp`, `synced.cpp`), the fake (`include/qiven/platform/fake/fake_platform.hpp`, `src/fake/fake_platform.cpp`) and their cases (`tests/win_facts_test.cpp`, `win_facts_machine_test.cpp`, `win_facts_faults_test.cpp`)
- **Holds:** the target `qiven_platform`: the platform contract (files, processes, time, pages, random bytes) with files' retries and names; its Windows adapter, `qiven_platform_windows`, built on Windows alone; `current`, the system's own adapter, which the build chooses by its target system, `qiven_platform_current`; its fake, `qiven_platform_fake`, on the C++ standard library alone; `qiven_platform_targets`, which makes the contract's target again over another memory; its own tests (`tests/`): files' retries and names over the fake, the fake's processes, and the Windows adapter's files, stage, names, arguments and facts over real files in each case's scratch directory, and its processes over the children of its own program, `qiven_platform_child`, each system call failed in turn; the product's `app`, not this module, chooses among the adapters one system offers
- **Never holds:** a store, entry or location; a caller's code, nor the cases over a caller's use; a product's checks over the machine but `facts`, `synced` and `residual`, until they leave
- **Contract:** `foundation/system/platform/include/qiven/platform/platform.hpp`
- **Files:** `include/qiven/platform/` (`platform.hpp`, `files.hpp`, `names.hpp`, `current.hpp`; `windows/`: `windows.hpp`, `files.hpp`; `fake/`: `fake_platform.hpp`, `fake_files.hpp`), `src/` (`platform.cpp`, `current.cpp`; `windows/`, the adapter's sources and private headers; `fake/`), `tests/` (its cases, `win_process_case.hpp` the process cases' helpers, and `child/`, the program their children run), `CMakeLists.txt`; their namespace `qiven::platform`, the adapter's `qiven::platform::win`, the fake's `qiven::platform::fake`
- **Consumers rely on:** concepts bound at compile time; views in, the caller's buffers out, on the caller's arena; typed `PlatformError` codes, never a system's number
- **Parameters:** `config_leaf` (not built yet); `open_bound` (not built yet)
- **Refuses:** a system it has no adapter for (`unsupported`); a stage past `open_bound` files already open
- **Words:**
  - `adapter`: a concrete type meeting `Platform` for one system; the fake is one
  - `stage`: a staged directory's files, written then replaced whole
