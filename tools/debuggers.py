"""The debugger tools/debug.py and the watcher (tools/watch.py) drive: found
and checked against its pin, and its output bounded.

- cdb.exe, the Windows SDK's, found from the SDK's root
  (foundation/devkit/python/system.py's sdk_root) and Debuggers/x64/, its version (cdb -version) and SHA-256 checked against
  tools/windows-pins.txt. It is run with -sins, so that no environment's
  symbol path or server is read, and -y the binary's directory. Linux's
  gdb is retired (tools/README.md's Retired section).
- Absent, the debugger is Absent, and its cases skip by name; present off
  its pin, or with no pin line, it is NotFound, and its cases fail, as any
  pin does.
- hang_stacks(): every thread's stack of a running process, by the
  debugger attached without stopping it, and a dump of it; the watcher
  writes them before it kills a case past its limit. Its debugger is the
  pinned one, but in a CI job (GitHub Actions' GITHUB_ACTIONS "true",
  which the runner sets in every step), where the image keeps its own
  tools: there the image's cdb, unpinned, named by its path and version
  (image_cdb()), or Absent naming where it looked.

Run by the pinned interpreter under -I, from its file (runpy): it imports
nothing of this directory, and Python's lowest layer by the root its runner
has already appended.
"""

import hashlib
import os
import re
from pathlib import Path

from foundation.devkit.python import answer
from foundation.devkit.python import process
from foundation.devkit.python import system
from foundation.devkit.python.build_tree import ROOT

WINDOWS_PINS = ROOT / "tools" / "windows-pins.txt"
FRAMES = 32
HANG_THREADS = 16
HANG_LINES = 200  # the hang's lines at most, its first line among them
HANG_SECONDS = 25.0  # the debugger's time, within the 30 s after the limit
VERSION_SECONDS = 30  # the debugger's -version
# A CI job: GitHub Actions sets GITHUB_ACTIONS to "true" in every step; the
# workflows set no variable of their own that names CI.
CI_VARIABLE = "GITHUB_ACTIONS"
CDB_VERSION = re.compile(r"\bversion (\d+(?:\.\d+)+)")
# A thread's header in cdb's ~*kn.
CDB_THREAD = re.compile(r"^[.#]?\s*\d+\s+Id: [0-9a-f]+\.[0-9a-f]+ ")
# cdb's lines that say nothing of the case.
NOISE = re.compile("|".join((
    r"^ModLoad: ", r"^\([0-9a-f]+\.[0-9a-f]+\): .* chance", r"^Breakpoint \d+ hit$",
    r"^\*\*\* WARNING: Unable to verify checksum", r"^NatVis script ", r"^quit:$")))


class NotFound(Exception):
    """No debugger to use, the reason: one off its pin, or Absent."""


class Absent(NotFound):
    """No debugger installed: "no cdb: ...", a skip."""


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def pin(path, kind, name):
    """(version, digest, source) of the pin file's line KIND NAME, or None."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        fields = line.split("#", 1)[0].split()
        if fields[:2] == [kind, name] and len(fields) == 5:
            return fields[2], fields[3], fields[4]
    return None


def checked(path, version, digest, ask):
    """The tool's path once its SHA-256 and version hold, else NotFound."""
    got = sha256(path)
    if got != digest:
        raise NotFound(f"{path.name}'s SHA-256 is {got}; the pin {digest}")
    try:
        done = process.run([path, *ask], limit=VERSION_SECONDS)
    except (OSError, process.TimedOut) as e:
        raise NotFound(f"{path.name} {' '.join(ask)}: {e}") from e
    text = done.out.decode("utf-8", "replace")
    if not re.search(rf"(?<![\w.]){re.escape(version)}(?![\w.])", text):
        raise NotFound(f"{path.name} {' '.join(ask)} gives {text.strip()[:80]!r}; the pin is {version}")
    return path


def find_cdb():
    """The debugger, checked, or NotFound (Absent when there is none)."""
    found = pin(WINDOWS_PINS, "debugger", "cdb")
    if found is None:
        raise NotFound(f"no line 'debugger cdb' in {WINDOWS_PINS.name}")
    version, digest, source = found
    root = system.sdk_root()
    if root is None:
        raise Absent("no cdb: no Windows SDK, the registry naming no KitsRoot10")
    path = root / source.removeprefix("kits/")
    if not path.is_file():
        raise Absent(f"no cdb: none in the SDK's {Path(source).parent.as_posix()}")
    return checked(path, version, digest, ["-version"])


def on_ci():
    """Whether this runs in a CI job: GITHUB_ACTIONS "true"."""
    return os.environ.get(CI_VARIABLE) == "true"


def image_cdb():
    """A CI image's own cdb, unpinned: cdb on PATH, else the SDK's
    Debuggers/x64/cdb.exe, the SDK's root WindowsSdkDir (vcvars sets it),
    else the registry's KitsRoot10; (path, version) by its -version.
    Absent naming where it looked; NotFound when its version cannot be
    read."""
    found = process.find("cdb")
    if found is None:
        sdk = os.environ.get("WindowsSdkDir")
        root, how = (Path(sdk), "WindowsSdkDir") if sdk else (system.sdk_root(), "the registry's KitsRoot10")
        if root is None:
            raise Absent("no cdb: none on PATH, and no Windows SDK: no WindowsSdkDir, the registry naming no "
                         "KitsRoot10")
        found = root / "Debuggers" / "x64" / "cdb.exe"
        if not found.is_file():
            raise Absent(f"no cdb: none on PATH, none at {found.as_posix()}, the SDK's root by {how}")
    path = Path(found)
    try:
        done = process.run([path, "-version"], limit=VERSION_SECONDS)
    except (OSError, process.TimedOut) as e:
        raise NotFound(f"{path.as_posix()} -version: {e}") from e
    text = done.out.decode("utf-8", "replace")
    version = CDB_VERSION.search(text)
    if version is None:
        raise NotFound(f"{path.as_posix()} -version gives no version: {text.strip()[:80]!r}")
    return path, version.group(1)


def hang_debugger():
    """The hang's debugger: (path, how), how None for the pinned one
    (find_cdb()); in a CI job the image's cdb (image_cdb()), how naming its
    version and path."""
    if on_ci():
        path, version = image_cdb()
        return path, f"by the image's cdb {version} at {path.as_posix()}, unpinned (CI)"
    return find_cdb(), None


def module(binary):
    """cdb's name of a binary's module: its stem, each character other
    than a letter, a digit or _ as _."""
    return re.sub(r"[^A-Za-z0-9_]", "_", Path(binary).stem)


def hang_command(debugger, pid, binary, dump):
    """The debugger's command line: every thread's stack, then the dump
    (none when dump is None), the process left running."""
    commands = f"~*kn {FRAMES}" + (f"; .dump /mt {dump}" if dump else "") + "; qd"
    return [str(debugger), "-pv", "-p", str(pid), "-sins", "-lines", "-y", str(Path(binary).parent),
            "-c", commands]


def hang_lines(text):
    """The stacks of the debugger's output: (threads, lines), the threads
    past HANG_THREADS dropped, the lines within HANG_LINES - 1."""
    lines = text.splitlines()
    start = next((i + 1 for i, line in enumerate(lines) if "Reading initial command" in line), len(lines))
    lines = lines[start:]
    threads, kept, dropped = 0, [], False
    for line in lines:
        if not line.strip() or NOISE.match(line):
            continue
        if CDB_THREAD.match(line):
            threads += 1
        if threads > HANG_THREADS and not line.startswith(("Creating ", "Dump ")):
            dropped = True
            continue
        kept.append(answer.clip(line))
    cut = f"{answer.CUT} {max(threads - HANG_THREADS, 0)} threads past {HANG_THREADS}, or lines past {HANG_LINES}"
    return threads, answer.within(kept, HANG_LINES - 1, cut, dropped)


def hang_dump(pid, binary, dumps):
    """The dump hang_stacks has the debugger write, <binary>-<pid>-hang.dmp
    in dumps; None when dumps is not given."""
    return Path(dumps) / f"{Path(binary).stem}-{pid}-hang.dmp" if dumps else None


def stacks_of(lines):
    """The threads whose stacks the hang's lines hold: their headers."""
    return sum(1 for line in lines if CDB_THREAD.match(line))


def hang_stacks(pid, binary, dumps):
    """Every thread's stack of the running process pid, its binary's
    symbols beside it, and its dump <binary>-<pid>-hang.dmp in
    dumps when it is given: (threads, lines, how), how hang_debugger()'s.
    NotFound when no debugger is at hand or it cannot run."""
    debugger, how = hang_debugger()
    dump = hang_dump(pid, binary, dumps)
    try:
        done = process.run(hang_command(debugger, pid, binary, dump), limit=HANG_SECONDS)
    except process.TimedOut as e:
        return (*hang_lines(e.out.decode("utf-8", "replace") + e.err.decode("utf-8", "replace")), how)
    except OSError as e:
        raise NotFound(f"{Path(debugger).name}: {e}") from e
    return (*hang_lines(done.out.decode("utf-8", "replace") + done.err.decode("utf-8", "replace")), how)
