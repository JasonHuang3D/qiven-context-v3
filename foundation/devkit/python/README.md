# python

layer devkit, status built

- **Holds:** Python's lowest layer, below every tool and test of the repository's Python: `status.py`, the exit statuses by name, the one table a status is named by; `system.py` with `_windows.py`, the operating system, and the one conditional on it, `system.WINDOWS`; `process.py`, a program started, bounded and ended, the one place `subprocess` stands above `_windows.py`; `git.py`, the one place a program starts git, through `process`; `table.py`, the one reader of the module table and of the trees `tools/trees.json` names
- **Never holds:** a product's or a tool's concept; a member with no consumer in the work at hand; a wrap of a portable standard-library call (`pathlib`, `json`, `tempfile`, `hashlib.file_digest`), which makes nothing easier, safer or more stable
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.python`, imported normally. A script run under `-I` appends the checkout's root once (`sys.path.append`, so that the standard library still wins) and then imports it; a test finds the root already there, as unittest's discover puts it. `status.py` also runs as a script, naming each status its arguments give
- **Files:** `__init__.py`, `status.py`, `system.py`, `_windows.py`, `process.py`, `git.py`, `table.py`, `tests/`, each test proving a member of this module, and this directory's `module.json`
- **Consumers rely on:** a status named in one line, by `line` and `ctest_line`, its value by `value_text` and its name by `named`, never as a bare number; a system's fact or act by its `system` member in the same words on every system, and `import system` succeeding on every system; every start above `_windows.py` through `process`: standard input closed unless given, a limit on every `run`, past which the program's whole tree is ended and `TimedOut` raised, output as bytes unless asked for as text; git through `git`'s verbs alone, every failure one `GitFailed`, no consumer passing git's argv or decoding its `-z`; the table through `table`, given the checkout's root, a file's module by `owners` and its test code by `test_path`, no consumer reading a `module.json` or `tools/trees.json` itself
- **Parameters:** none
- **Refuses:** as a script, no argument, or an argument that is no number: its usage or its reason, status 2. `system`, on a system with no back end: each member, `NotBuilt`. `process`: a program that cannot start, the system's `OSError`; a run past its limit, `TimedOut`. `git`: no git, a failed start, a limit passed, a status other than 0 or an answer that is no UTF-8, `GitFailed`. `table`: a `tools/trees.json` that cannot be read, the system's `OSError`; one that does not parse or lacks or adds a field, `ValueError`; `load`, a table file in error, `ValueError`

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

The method for `process`:

- **Members:** `find(name, path=None)`, `run(argv, *, data=None, env=None, cwd=None, limit=LIMIT, out=PIPE, err=PIPE, program=None, text=False)`, `start(argv, *, feed=False, env=None, cwd=None, out=PIPE, err=PIPE)`; `Ran` (`status`, `out`, `err`, `named`, its `str`), `Process` (`pid`, `stdin`, `stdout`, `stderr`, `poll()`, `wait(limit=LIMIT)`, `end_tree()`), `TimedOut` (`limit`, `out`, `err`); `PIPE` and `INHERIT`, the values `out` and `err` take beside a file, and `MERGE`, which `err` also takes, into `out`; `LIMIT`, 120 seconds, and `ENDING_SECONDS`, 10. `argv` is a list of `str`, `Path`, number or `bytes`, or one `str`, a whole command line, for a compile database's command
- **Compatibility:** no conditional on the system: each call is the standard library's or `system`'s. Windows built; POSIX and macOS on paper, no Linux build:

| Member | Windows | POSIX on paper | macOS on paper, where it differs |
|---|---|---|---|
| `find` | `shutil.which`: `PATH`, and `PATHEXT` for the suffix | `shutil.which`: `PATH` and the execute bit | |
| `run`, `start` | `subprocess.Popen`: `CreateProcessW` | `subprocess.Popen`: `fork_exec` or `posix_spawn`; `process_group=0` comes on the one start, from the back end, when `end_tree` is built there | |
| a `bytes` word of `argv` | `os.fsdecode`, then the command line's UTF-16 | the bytes themselves | |
| one `str` as `argv` | the command line, as `CreateProcessW` takes it | the program's name alone, `subprocess`'s reading: its one consumer, `records`, reads MSVC's database, Windows' alone | |
| `program` | `CreateProcessW`'s application name | `execve`'s path | |
| standard input closed | `NUL` | `/dev/null` | |
| `run`'s limit | `communicate`'s join of its reader threads, each one whole read: what a program wrote comes only once its pipe ends, so after `end_tree` | `communicate`'s selector, which also holds what came before the limit | |
| ending the tree at the limit | `system.end_tree`: `taskkill /T /F /PID`, then `TerminateProcess` of the program | `system.end_tree`: `killpg`, then `SIGKILL` of the program; off Windows today, `NotBuilt` at the limit, after the program itself is ended | |
| `Process.poll`, `Process.wait` | `WaitForSingleObject`, `GetExitCodeProcess` | `waitpid` | |

  A status is as `subprocess` gives it, `status`'s table naming it. Text is UTF-8 with what does not decode replaced, each line's ending `\n`, the one reading for the runs that ask for it, `changed`'s and `test_trees_build`'s. A system program's output in the ANSI code page is read from the bytes by `system.output_text`, not here
- **Extensibility:** a new program needs one call and no new member. Another destination of output is another value of `out` or `err`, no new signature. A start option a second system needs is one line in the one start, read from `system`; no consumer changes
- **Performance:** its import loads `subprocess` and `shutil`, about 13 ms here with `status`, measured once by `-X importtime`, the two a cost the watcher paid already in front of each test; `Ran` is a `namedtuple`, so that a test compares it to a tuple. `_windows.py` loads only when a limit passes or a member asks. A `run` costs one start and a reader thread for each pipe, its output held in memory: a consumer of large output gives a file as `out`. `start` costs one start and a small object, and `poll` one call, each `POLL` seconds of a watched run. Nothing here is on a hot path
- **A fake:** none: every consumer is a test or a tool that exists to start the real program, and a test of a fake here would prove the fake. `run`'s limit is proven by this module's own test, `tests/test_process.py`; the rest of `process` by use, its consumers' tests, the watcher's hang cases and the debugger's among them, under their own labels
- **Errors:** a program that cannot start raises the system's `OSError` unwrapped, as `open` does; each consumer words it with its own name. A `run` past `limit`, or a `wait` past its own, raises `TimedOut`: "process: <program> past its limit of <limit> s". At a `run`'s limit `system.end_tree` ends the tree and the program itself is ended, and then what it wrote is collected, within `ENDING_SECONDS`, into `TimedOut`'s `out` and `err`; past those seconds a process the tree no longer reaches holds the pipe (its parent gone before the limit), and they are empty, the pipes left to the process's end. `system.Failed` at an ending is no error: the program itself is ended next. Anything else that interrupts a `run`, an interrupt among them, ends the program alone, not its tree, and goes on. `Process.wait` past its limit ends nothing: the caller chooses. `Process.end_tree` waits for the program, `ENDING_SECONDS` at most, and answers its status, or `None` where it has not ended by then. A status other than 0 is no error: the caller reads `status`

The method for `git`:

`git.py` owns git, through `process`: every start of git, its argv, its decoding and its error. It holds `find`, `tracked`, `attributes`, `full_hash`, `files_at`, `reading_env` and `GitFailed`, each with a consumer in the tree.

- **Compatibility:** git is one program with one command line on every system; a member's system difference lies in finding it, in the names it gives and in its environment's names:

| Member | Windows | POSIX on paper | macOS on paper, where it differs |
|---|---|---|---|
| `find` | `process.find`: `shutil.which`, `PATH` with `PATHEXT` (`git.exe`), the working directory first, as Windows finds a program | `shutil.which`, `PATH` alone | `/usr/bin/git` may be Xcode's stub, which finds as git and fails its first run: `GitFailed` |
| `tracked` | `ls-files -z`, with `-co --exclude-standard` for `others`, from `root`; names in UTF-8 whatever the code page, each `/` | the same call; a name is the file system's bytes, and one that is no UTF-8 is `GitFailed` | the same; git's `core.precomposeunicode` governs how a name reads |
| `attributes` | `check-attr -z --stdin <names>`, from `root` | the same | |
| `full_hash` | `--git-dir <root>/.git rev-parse --verify <commit>^{commit}` | the same | |
| `files_at` | `--git-dir <root>/.git ls-tree -r -z --name-only <commit>^{commit}`, then `cat-file --batch` | the same | |
| `reading_env` | the inherited environment less each `GIT_` name, read without regard to case as Windows' names are; `GIT_CONFIG_NOSYSTEM`, `GIT_ATTR_NOSYSTEM`, `GIT_CONFIG_GLOBAL`, `GIT_CONFIG_COUNT` and its `KEY` and `VALUE` pairs | the same call; the comparison without regard to case also drops a `git_` name, which git does not read | |

  `reading_env` needs a git that reads `GIT_CONFIG_COUNT` and `GIT_CONFIG_GLOBAL`; here 2.55.0.windows.5, the versions that first read them unverified. `full_hash` and `files_at` name `<root>/.git` so that a directory that is no repository fails rather than finding a parent's; `tracked` and `attributes` read as git finds the repository from `root`
- **Extensibility:** a new need is one more verb over the one private run, its argv and its decoding in this file, with a consumer in the tree when it is built; no consumer passes argv or decodes `-z`. A second system changes nothing here unless it is one of the differences above
- **Performance:** each verb is one process but `files_at`, two, each with one walk of `PATH` to find git. They answer a tool's or a test's one question over a checkout and are never on a hot path: `tracked` over this checkout, 785 files, took about 26 ms here, measured once. Importing `git` costs `process`'s import and `pathlib`'s. A verb holds git's whole output in memory, the table files' sizes for `files_at`, a list of names for `tracked`
- **A fake:** none: every consumer exists to read the real repository, and a test of a fake here would prove the fake. A missing git is a `PATH` that holds none
- **Errors:** every failure is `GitFailed`, one line, and each consumer answers it once in its own form: `git: none on PATH`; `git <argv>: <the status's line>: <the end of git's stderr>`; `git <argv>: <the reason>` for a start that failed or a limit passed, 300 s, past which `process` ends git's tree; `git <argv>: its answer is no UTF-8: <the codec's reason>`; `git <argv>: no file at <path>: <git's line>` for a `cat-file` answer that holds no blob. `find` answers `None` for no git, for a consumer that goes on without it. `reading_env` raises the system's `OSError` when its scratch directory cannot take the empty file, which is the caller's directory and no failure of git

The method for `table`:

`table.py` reads the module table from its files, given a checkout's root, and holds no word of what lies in the checkout: every tree, suffix and vendored file comes from the root's `tools/trees.json`, the repository's data. It holds `trees` and its `Trees` (the file's fields, `every`, `cxx_suffixes`, `test_path`, `is_table_file`, `table_files`), `tree`, `load`, `owners`, `holds` and `own_paths`, each with a consumer in the tree. What the table's words mean against the tree, the build and the base is the modules' check's, `foundation/devkit/structure/modules.py`, above this module.

- **Compatibility:** no conditional on the system: each call is the standard library's, and every path it answers has `/`:

| Member | Windows | POSIX on paper | macOS on paper, where it differs |
|---|---|---|---|
| `trees`, `tree`, `load` | `Path.read_text`: `CreateFileW`, `ReadFile`; UTF-8 whatever the code page | `open`, `read`; the same text | |
| `Trees.table_files` | `Path.rglob`: `FindFirstFileW`, the name matched without regard to case | `scandir`, the name by exact case, so that a `Module.json` is no table file | the same call; on a volume without regard to case, whether a `Module.json` is found is not verified |
| `owners`, `holds`, `Trees.test_path`, `Trees.is_table_file` | no call: strings, `fnmatch.fnmatchcase`, by exact case | the same | |

- **Extensibility:** a new tree, suffix or table file outside the layers is a value in `tools/trees.json`, no code here. A new field of that file is a field of `Trees`, refused until it is one, so that no reader goes on without it. A new key of a table file is one more name in `KEYS`, and a new rule over the table the check's. A second system changes nothing here
- **Performance:** its import loads `json`, `fnmatch` and `pathlib`, about 12 ms here, measured once. `load` over this checkout, 64 table files and 68 rows, took about 19 ms, measured once, a table's read once a process; `owners` over its 786 tracked files, a walk of every row's paths for each, about 104 ms, measured once, so `structure`'s `layers` keeps each answer. `test_path` is a string search, each file's answer under a microsecond. Nothing holds a cache: a consumer that asks again holds its `Trees` or its table
- **A fake:** none: every consumer exists to read the real checkout's table, and a test of a fake here would prove the fake. A tree with another table is a directory holding those files. Its proof is by use: a change to `table.py` reruns the listing step, which reads the table and fails the build; `tools/changed.py` reads it first; `structure`'s and the runner's tests read it under their own labels, and `structure`'s `test_modules` holds the whole table against the checkout
- **Errors:** `tree` answers a list of lines and goes on, a table file in error giving no row: `table file <path>: does not parse: <reason>`, `table file <path>: is not a JSON object`, `table file <path>: status <status> is none of ...`, `table file <path>: lacks ...; adds ...`, `table file <path>: <field> ... is not ...`, `table file <path>: name <name> is <path>'s too`, `table file <path>: exception from <name> names no row of its file`, `row <name>: in <path> and <path>`, `row <name>: in <path>, not in <path>, the table file of its name`, `table file <path>: of <name> names no row`, `table file <path>: from <name> names no row`. `load` raises `ValueError`: `the table: <the first line>, and <n> more`. `trees` raises `ValueError`, `table: tools/trees.json: <reason>`, and a file that cannot be read the system's `OSError` unwrapped, as `open` does; a table file that cannot be read too, each consumer wording it with its own name. A file in no module is no error: `owners` answers its empty list, and each consumer says what that means
