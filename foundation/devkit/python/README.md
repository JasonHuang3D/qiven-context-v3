# python

layer devkit, status built

- **Holds:** Python's lowest layer, below every tool and test of the repository's Python: `status.py`, the exit statuses by name, the one table a status is named by
- **Never holds:** a product's or a tool's concept; a member with no consumer in the work at hand; a wrap of a portable standard-library call (`pathlib`, `json`, `tempfile`, `hashlib.file_digest`), which makes nothing easier, safer or more stable
- **Contract:** none
- **Interface:** no header: the package `foundation.devkit.python`, imported normally. A script run under `-I` appends the checkout's root once (`sys.path.append`, so that the standard library still wins) and then imports it; a test finds the root already there, as unittest's discover puts it. `status.py` also runs as a script, naming each status its arguments give
- **Files:** `__init__.py`, `status.py`, and this directory's `module.json`
- **Consumers rely on:** a status named in one line, by `line` and `ctest_line`, its value by `value_text` and its name by `named`, never as a bare number
- **Parameters:** none
- **Refuses:** as a script, no argument, or an argument that is no number: its usage or its reason, status 2

The method, for the package as a whole. `system`, the member that wraps the operating system, is not built yet, and states its own when it is.

- **Compatibility:** `status` calls no system: it names the values `subprocess` gives. Windows: the exit code, a system status such as `0xC0000005`. Linux: a negative value for a death by a signal, its numbers x86-64's; on paper, no Linux build. macOS: the same form on paper, where a number that differs from Linux's (`SIGBUS`) needs its own row
- **Extensibility:** a second system's status is a row of the table, and `line`, `named`, `value_text` and `ctest_line` keep their forms: no contract changes
- **Performance:** two dictionary reads and a format, once a process has ended; never on a hot path. Its import is a small module's, once a process, in front of each test the watcher runs
- **A fake:** none: the table is data with no system behind it, and no consumer needs another
- **Errors:** a status off the table is no error: it is named `unnamed`. As a script its errors are one line under its own name; the words after the name, for an argument that is no number, are still the interpreter's `ValueError`
