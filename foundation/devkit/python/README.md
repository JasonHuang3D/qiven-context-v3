# python

layer devkit, status built

- **Holds:** Python's lowest layer, below every tool and test of the repository's Python: `status.py`, the exit statuses by name, the one table a status is named by; `system.py` with `_windows.py`, the operating system, and the one conditional on it, `system.WINDOWS`
- **Never holds:** a product's or a tool's concept; a member with no consumer in the work at hand; a wrap of a portable standard-library call (`pathlib`, `json`, `tempfile`, `hashlib.file_digest`), which makes nothing easier, safer or more stable
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.python`, imported normally. A script run under `-I` appends the checkout's root once (`sys.path.append`, so that the standard library still wins) and then imports it; a test finds the root already there, as unittest's discover puts it. `status.py` also runs as a script, naming each status its arguments give
- **Files:** `__init__.py`, `status.py`, `system.py`, `_windows.py`, and this directory's `module.json`
- **Consumers rely on:** a status named in one line, by `line` and `ctest_line`, its value by `value_text` and its name by `named`, never as a bare number; a system's fact or act by its `system` member in the same words on every system, and `import system` succeeding on every system
- **Parameters:** none
- **Refuses:** as a script, no argument, or an argument that is no number: its usage or its reason, status 2. `system`, on a system with no back end: each member, `NotBuilt`

The method for `status`:

- **Compatibility:** `status` calls no system: it names the values `subprocess` gives. Windows: the exit code, a system status such as `0xC0000005`. Linux: a negative value for a death by a signal, its numbers x86-64's; on paper, no Linux build. macOS: the same form on paper, where a number that differs from Linux's (`SIGBUS`) needs its own row
- **Extensibility:** a second system's status is a row of the table, and `line`, `named`, `value_text` and `ctest_line` keep their forms: no contract changes
- **Performance:** two dictionary reads and a format, once a process has ended; never on a hot path. Its import is a small module's, once a process, in front of each test the watcher runs
- **A fake:** none: the table is data with no system behind it, and no consumer needs another
- **Errors:** a status off the table is no error: it is named `unnamed`. As a script its errors are one line under its own name; the words after the name, for an argument that is no number, are still the interpreter's `ValueError`

The method for `system`:

- **Compatibility:** each member's call, Windows' built, POSIX's and macOS's on paper, with no Linux build:

| Member | Windows | POSIX on paper | macOS on paper, where it differs |
|---|---|---|---|
| `WINDOWS` | `sys.platform` | the same read | |
| `program_file` | the name and `.exe` | the bare name | |
| `library_file` | the name and `.dll` | `lib<name>.so` | `lib<name>.dylib` |
| `quiet_load_errors` | `SetErrorMode` | nothing | |
| `end_tree` | `taskkill /T /F /PID` | `killpg` with `SIGKILL`, the case started with `process_group=0` | |
| `children` | Toolhelp32 | `/proc/<pid>/task/<pid>/children` | `proc_listchildpids` |
| `visible_windows` | `EnumWindows`, `GetWindowThreadProcessId`, `IsWindowVisible` | always empty | |
| `alive` | `OpenProcess`, `GetExitCodeProcess` | `kill(pid, 0)`, `/proc` stat | `kill(pid, 0)` |
| `loader_dirs` | the binary's directory, `System32`, `SystemRoot`, `PATH` | `LD_LIBRARY_PATH`, `ld.so.conf` | `DYLD_LIBRARY_PATH` |
| `sdk_root` | the registry's `KitsRoot10` | none | |
| `toolset_env` | `vswhere`, `cmd`, `vcvars64.bat` | the inherited environment | |
| `handle_count` | `OpenProcess`, `GetProcessHandleCount` | the entries of `/proc/<pid>/fd` | `proc_pidinfo` with `PROC_PIDLISTFDS` |
| `plant_handle` | `OpenProcess`, `DuplicateHandle` into the process | none: no call puts a descriptor into another process, and its case is Windows' alone | |
| `fail_fast_status` | `0xC0000409`, `__fastfail`'s | `-SIGABRT` | |
| `missing_library_status` | `0xC0000135` | `127`, the dynamic loader's exit | `-SIGABRT`, dyld's abort |
| `invalid_access_status` | `0xC0000005` | `-SIGSEGV` | `-SIGSEGV` or `-SIGBUS`, by the fault |
| `output_text`, `output_bytes` | the ANSI code page, Python's `mbcs` | UTF-8 | |
| `drives` | `GetLogicalDrives`, `GetDriveTypeW` | none: no drive letters, and its cases are Windows' alone | |
| `make_junction` | `_winapi.CreateJunction` | none: a junction is NTFS's; a symbolic link is `os.symlink`'s, portable | |
| `map_drive`, `unmap_drive` | `subst X: <dir>`, `subst X: /D` | none: no drive letters | |

  The statuses are as `subprocess` gives them, a signal's negative. `drives` answers each letter's kind in the module's words: `fixed`, `removable`, `remote`, `optical`, `memory`, `no root` or `unknown`. `start_options` is not built: its Windows form is nothing. Its POSIX call, `process_group=0` on the start, comes with the POSIX back end, for `end_tree`
- **Extensibility:** a second system is one more back end beside `_windows.py` and one more branch in `system.py`'s `_back`; no member's signature changes and no consumer changes
- **Performance:** reading `WINDOWS` is an attribute, and importing `system` loads only `sys`. `_windows.py` loads when a member is first asked, once a process: its import, with `ctypes`, took about 24 ms here, measured once, which the watcher paid in front of each test already. Then a member costs one function call above its system call; `visible_windows` loads `user32` only when asked, so the watcher never loads it. The members in a loop are `children` and `visible_windows`, each poll of a watched run, every `POLL` seconds; `program_file` and `library_file` are a string's join, and the three statuses a constant. `handle_count` runs twice a drive's test, `drives` once a machine case; `end_tree`, `toolset_env`, `map_drive` and `unmap_drive` start programs and are never on a hot path
- **A fake:** none: every consumer is a test or a tool that exists to meet the real system, and a test of a fake here would prove the fake
- **Errors:** `NotBuilt` names the member and the system. `Failed` names the member, the call and the system's reason; `alive`, `children`, `visible_windows`, `end_tree`, `toolset_env`, `handle_count`, `plant_handle`, `drives`, `make_junction`, `map_drive` and `unmap_drive` raise it. `output_text` and `output_bytes` raise the codec's own `UnicodeError` under `errors="strict"`, as `bytes.decode` and `str.encode` do. `sdk_root` answers `None` for no SDK; `end_tree` does not read `taskkill`'s status, so a process gone already and one it could not end read alike, and each consumer ends its own child next. `alive` reads a process that ended with status 259, `STILL_ACTIVE`, as running, the call's own limit. `quiet_load_errors`' call has no failure to report
