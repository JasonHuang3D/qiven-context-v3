"""Runs a binary of the build: its arguments, its bytes in and out, its
exit status.

CTest gives each Python test the build's directories by the environment:
QIVEN3_BIN, where every binary of the build is written; QIVEN3_BUILD, the
build directory; QIVEN3_CTEST, the ctest of the build. A test run outside
CTest names them itself. A status is named by tools/status.py's table, and
watched() runs a command behind the watcher, tools/watch.py, and
unbounded() drops the lines of its dumps' bound (tools/dumps.py), which
another case's dump, written beside, may give any run.
"""

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from tools import status as status_table

ROOT = Path(__file__).resolve().parents[2]

# A process the tests start ends within this many seconds, or fails.
TIMEOUT = 120


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
    suffix = ".exe" if sys.platform == "win32" else ""
    found = directory("QIVEN3_BIN") / (name + suffix)
    if not found.is_file():
        raise RuntimeError(f"the build holds no binary {name}: {found}")
    return found


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
