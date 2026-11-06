"""Harness' binaries, of the runner's tool half, which the Python tests run the build by:
a binary of the build found and run, its skip code SKIP_CODE; a build
directory's options read; and a command run behind the watcher, the windows
it shows watched. A program starts through foundation/devkit/python/process.py.

CTest gives each Python test the build's directories by the environment:
QIVEN_BIN, where every binary of the build is written; QIVEN_BUILD, the
build directory; QIVEN_CTEST, the ctest of the build, which
foundation/devkit/python/build_tree.py reads with the cmake beside it and
the build's listing. A test run outside
CTest names them itself. The watcher is WATCHER, watch.py beside this
file, and its dumps' bound dumps.py, beside it.

listed() gives the files of the build's binaries.txt. build_option() reads
a cache variable of the build by foundation/devkit/python/build_tree.py, and
has_faults(), debug_library() and sanitized() the build's
choices by it. run_watched() runs a command behind the watcher under a
limit, TIME_BOUND unless given, polling it every POLL seconds for the
windows it shows, drops the dumps' bound's lines from its standard error,
and ends the watcher's tree past the limit, its AFTER seconds and AFTER
more; a report's line holds at most
foundation/devkit/python/answer.py's LINE_POINTS code points.
"""

import os
import sys
import tempfile
import time
from pathlib import Path

from foundation.devkit.python import build_tree
from foundation.devkit.python import process
from foundation.devkit.python import system
from foundation.devkit.python.build_tree import ROOT

# A process the tests start ends within this many seconds, or fails.
TIMEOUT = process.LIMIT

# A case binary's skip, the C++ runner's exit_skip
# (foundation/devkit/test/runner/include/qiven/runner/core.hpp).
SKIP_CODE = 125

# The watcher, in front of every test.
WATCHER = ROOT / "foundation" / "devkit" / "test" / "harness" / "watch.py"

# A watched run's bound in seconds and its poll's period; the watcher's
# seconds past its limit at most, the 30 s CTest leaves it
# (ctest_tests.cmake's AFTER_LIMIT).
TIME_BOUND = 10.0
POLL = 0.1
AFTER = 30.0


def directory(name):
    """The directory the environment variable `name` gives; it must exist."""
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"{name} is not set: run the test through ctest")
    found = Path(value)
    if not found.is_dir():
        raise RuntimeError(f"{name} names no directory: {value}")
    return found


def path(name):
    """The file of the build's binary `name`."""
    found = directory("QIVEN_BIN") / system.program_file(name)
    if not found.is_file():
        raise RuntimeError(f"the build holds no binary {name}: {found}")
    return found


def listed():
    """The files of the build's binaries.txt, in its order, as resolved paths."""
    text = (directory("QIVEN_BUILD") / "binaries.txt").read_text(encoding="utf-8")
    return [Path(line).resolve() for line in text.splitlines() if line]


def run(name, args=(), data=None, cwd=None, timeout=TIMEOUT):
    """Runs the build's binary `name`: process.run's Ran."""
    return process.run([path(name), *args], data=data, cwd=cwd, limit=timeout)


def unittest_run(*args, timeout=TIMEOUT):
    """Runs this interpreter's unittest from the root, as CTest does."""
    return process.run([sys.executable, "-I", "-m", "unittest", *args], cwd=ROOT, limit=timeout)


def build_option(name):
    """A cache variable of the build, QIVEN_BUILD, or None when the build
    has no cache or its cache does not set it."""
    return (build_tree.options(directory("QIVEN_BUILD")) or {}).get(name)


def has_faults():
    """Whether the build has QIVEN_FAULTS on, which is off unless set."""
    value = build_option("QIVEN_FAULTS")
    return value is not None and value.upper() in ("ON", "1", "TRUE", "YES", "Y")


def debug_library():
    """Whether the build uses MSVC's debug library, whose debug iterators
    give each container a proxy that its constructor from an allocator,
    noexcept, takes from the allocator: a budget or a fault met there ends
    the process, so the budgets' plants and the fault sweeps do not apply."""
    return system.WINDOWS and (build_option("CMAKE_BUILD_TYPE") or "") == "Debug"


def sanitized():
    return bool(build_option("QIVEN_SANITIZE"))


def windows_of(pid):
    """The visible top-level windows of process `pid` and of its children,
    the watcher's case among them."""
    return system.visible_windows({pid, *system.children(pid)})


def run_watched(command, name, env=None, cwd=None, limit=TIME_BOUND):
    """Runs a command behind the watcher, by this interpreter as CTest runs
    it, under limit seconds: the watcher's status, out, err without the
    dumps' bound's lines (bytes; another case's dump, written beside, may
    give them any run), seconds, and the windows seen. A watcher still
    running past the limit, its AFTER seconds and AFTER more is ended with
    its tree."""
    watcher_command = [sys.executable, "-I", "-X", "faulthandler", str(WATCHER), "--limit", str(limit),
                       "--name", name, "--", *map(str, command)]
    start = time.monotonic()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        watcher = process.start(watcher_command, out=out, err=err, env=env, cwd=cwd)
        seen = []
        while watcher.poll() is None:
            seen += windows_of(watcher.pid)
            if time.monotonic() - start > limit + 2 * AFTER:
                watcher.end_tree()
                break
            time.sleep(POLL)
        status = watcher.poll()
        seconds = time.monotonic() - start
        out.seek(0)
        err.seek(0)
        unbounded = b"".join(line for line in err.read().splitlines(keepends=True)
                             if not line.startswith((b"dumps: removed ", b"dumps: cannot remove ")))
        return status, out.read(), unbounded, seconds, seen
