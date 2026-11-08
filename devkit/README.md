# devkit

devkit is the workbench a package is built, checked and tested with. It is
one package: a core that names no language (choosing which tests to run,
running them, the gate that decides whether a change may land, the store of
past results, the checks of a package's boundaries, and the order of the
development loop) and one part per language, which builds, installs and
lists the tests of a package in that language. devkit holds no fact of the
project it serves: every path, interpreter, tree, gate, variant, rule and
version pin comes from its caller as data, and devkit searches for nothing,
neither a root above it nor a default path.

## Requirements

- CPython 3.11 or later: the code uses `tomllib` (3.11) and relies on `-I`
  leaving the script's directory off `sys.path` (3.11). Only CPython 3.15.0
  on Windows has run it.
- No third-party package, no build backend and no network.

## Install

devkit installs itself, with the standard library only: no pip, no build
backend, no network and no console launcher. Its installer,
`qiven_devkit.install`, copies every file under `src/` into the purelib
directory the target interpreter names when asked, and records the install
in a `.dist-info` (METADATA, INSTALLER, RECORD) as the PyPA specification
"Recording installed projects" says, so `importlib.metadata` sees it. It
writes a file only where the content differs. It removes only what the
previous install's RECORD lists and the source no longer holds, and never a
path outside purelib. The target is any Python environment, a venv made
without pip included.

An installed devkit installs a new one, its source tree and the target
interpreter given:

```
<installed-python> -I -m qiven_devkit.install <devkit> <target-python>
```

Bootstrap, only for the first install, when no devkit is installed
anywhere: `<target-python> -I <devkit>/src/qiven_devkit/install.py <devkit> <target-python>`.

The installer handles a `[project]` table that holds only `name`, `version`,
`description` and `requires-python`, and refuses any other key: no
dependency, no entry point. Once devkit takes a dependency, its install is
designed again, with a lock.

Run devkit as `python -m qiven_devkit.<command>` from the environment it is
installed into.

## Run its tests

devkit's tests run from the environment devkit is installed into, against
that installed copy; the source tree is never on `sys.path`:

```
<python> -I -B -m unittest discover -s <devkit>/tests -t <devkit>/tests -v
```

`<python>` is the environment's interpreter and `<devkit>` this folder; `-B`
keeps bytecode out of the source tree. A test imports `qiven_devkit` as
installed; it never adds `src/` to the path.

## Layout

```
devkit/
  README.md           this specification
  pyproject.toml      the package's metadata: no dependency, no launcher
  src/qiven_devkit/   the import package
    install.py        devkit's own installer
    wrap/             the one way devkit starts a foreign program: the call
                      base, a wrapper and its fake per tool (its README.md)
  tests/              devkit's tests, run by unittest as above
```
