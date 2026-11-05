"""The runner's Python half beside skip.py, the harness the Python tests
run the build by: a binary of the build run, its arguments, its bytes in
and out, its exit status; a build directory's options read; and a command
run behind the watcher, the windows it shows watched.

CTest gives each Python test the build's directories by the environment:
QIVEN_BIN, where every binary of the build is written; QIVEN_BUILD, the
build directory; QIVEN_CTEST, the ctest of the build. A test run outside
CTest names them itself. A status is named by foundation/devkit/python/status.py's table, and
watched() runs a command behind the watcher, tools/watch.py, and
unbounded() drops the lines of its dumps' bound (tools/dumps.py), which
another case's dump, written beside, may give any run.

listed() gives the files of the build's binaries.txt. build_option() reads
a cache variable of a build directory, and
has_faults(), debug_library(), has_hal() and sanitized() the build's
choices by it. run_watched() runs a command behind the watcher under
TIME_BOUND, polling it every POLL seconds for the windows it shows; a
report's line holds at most LINE_POINTS code points.
"""

import os
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

from foundation.devkit.python import status as status_table
from foundation.devkit.python import system

ROOT = Path(__file__).resolve().parents[4]

# A process the tests start ends within this many seconds, or fails.
TIMEOUT = 120

# A watched run's bound in seconds, its poll's period, and a report's line's
# bound in code points.
TIME_BOUND = 10.0
POLL = 0.1
LINE_POINTS = 300


@dataclass(frozen=True)
class Ran:
    status: int
    out: bytes
    err: bytes

    @property
    def named(self):
        """The status's line by the table."""
        return status_table.line(self.status)

    def __str__(self):
        return f"{self.named}; stderr: {self.err[-2000:].decode('utf-8', 'replace')}"


def directory(name):
    """The directory the environment variable `name` gives; it must exist."""
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"{name} is not set: run the test through ctest")
    found = Path(value)
    if not found.is_dir():
        raise RuntimeError(f"{name} names no directory: {value}")
    return found


def tool(name):
    """The file the environment variable `name` gives; it must exist."""
    value = os.environ.get(name)
    if not value or not Path(value).is_file():
        raise RuntimeError(f"{name} names no file: run the test through ctest")
    return Path(value)


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


def run_file(program, args=(), data=b"", cwd=None, timeout=TIMEOUT):
    """Runs `program` with `args`, `data` on its input; its status and bytes."""
    done = subprocess.run(
        [str(program), *map(str, args)],
        input=data,
        capture_output=True,
        cwd=cwd,
        timeout=timeout,
        check=False,
    )
    return Ran(done.returncode, done.stdout, done.stderr)


def run(name, args=(), data=b"", cwd=None, timeout=TIMEOUT):
    """Runs the build's binary `name`."""
    return run_file(path(name), args, data, cwd, timeout)


def unittest_run(*args, timeout=TIMEOUT):
    """Runs this interpreter's unittest from the root, as CTest does."""
    return run_file(sys.executable, ["-I", "-m", "unittest", *args], cwd=ROOT, timeout=timeout)


def watched(command, name, limit=TIMEOUT):
    """The watcher's command line in front of `command`, run by this
    interpreter as CTest runs it."""
    return [sys.executable, "-I", "-X", "faulthandler", str(ROOT / "tools" / "watch.py"), "--limit", str(limit),
            "--name", name, "--", *map(str, command)]


def unbounded(err):
    """The watcher's standard error without the dumps' bound's lines."""
    return b"".join(line for line in err.splitlines(keepends=True)
                    if not line.startswith((b"dumps: removed ", b"dumps: cannot remove ")))


def build_option(name, build=None):
    """A cache variable of a build directory (QIVEN_BUILD's by default), or
    None when the build's cache does not set it."""
    build = Path(build) if build else directory("QIVEN_BUILD")
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(name + ":"):
            return line.split("=", 1)[1].strip()
    return None


def has_faults(build=None):
    """Whether the build has QIVEN_FAULTS on, which is off unless set."""
    value = build_option("QIVEN_FAULTS", build)
    return value is not None and value.upper() in ("ON", "1", "TRUE", "YES", "Y")


def debug_library(build=None):
    """Whether the build uses MSVC's debug library, whose debug iterators
    give each container a proxy that its constructor from an allocator,
    noexcept, takes from the allocator: a budget or a fault met there ends
    the process, so the budgets' plants and the fault sweeps do not apply."""
    return system.WINDOWS and (build_option("CMAKE_BUILD_TYPE", build) or "") == "Debug"


def has_hal(build=None):
    """Whether the build has the HAL: QIVEN_HAL is on unless set off."""
    value = build_option("QIVEN_HAL", build)
    return value is None or value.upper() not in ("OFF", "0", "FALSE", "NO", "N")


def sanitized():
    return bool(build_option("QIVEN_SANITIZE"))


def windows_of(pid):
    """The visible top-level windows of process `pid` and of its children,
    the watcher's case among them."""
    return system.visible_windows({pid, *system.children(pid)})


def run_watched(command, name, env=None, cwd=None):
    """Runs a command behind the watcher, under the time bound: the
    watcher's status, out, err, seconds, and the windows seen."""
    start = time.monotonic()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        process = subprocess.Popen(watched(command, name, TIME_BOUND), stdin=subprocess.DEVNULL,
                                   stdout=out, stderr=err, env=env, cwd=cwd)
        seen = []
        while process.poll() is None:
            seen += windows_of(process.pid)
            if time.monotonic() - start > TIME_BOUND + 20:
                process.kill()
                break
            time.sleep(POLL)
        status = process.wait()
        seconds = time.monotonic() - start
        out.seek(0)
        err.seek(0)
        return status, out.read(), unbounded(err.read()), seconds, seen
