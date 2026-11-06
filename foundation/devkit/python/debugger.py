"""The debugger, for the repository's Python: the one place cdb.exe is found,
checked against its pin and driven, for the
debugger agents drive (tools/debug.py) and the watcher's hang path
(foundation/devkit/test/runner/watch.py). This directory's README states each member's call on each
system; Linux's gdb is retired (tools/README.md's Retired section).

find() is the pinned cdb: the line "debugger cdb" of debugger_pins.txt,
beside this file, the file the environment variable QIVEN_CDB names, else the
SDK's under its root (system.sdk_root), its SHA-256 and its -version checked;
Absent, a NotFound, where neither exists, and NotFound where one is off its
pin, the line naming the variable, where the variable names no file, or where
there is no pin line: a machine with an SDK and no variable fails, never
skips. It runs with -sins, so that no environment's symbol path or server is
read, and -y the binary's directory.
hang_stacks(pid, binary, dumps) is every thread's stack of a running
process, by the debugger attached without stopping it, and its dump
(hang_dump), bounded: at most HANG_THREADS threads and HANG_LINES lines, each
line by answer. Its debugger is hang_debugger(): the pinned one, but in a CI
job (GitHub Actions' GITHUB_ACTIONS "true", which the runner sets in every
step), where the image keeps its own tools, the image's cdb, unpinned, named
by its path and version. Finding it and the stacks share one limit,
HANG_SECONDS, so that the watcher ends within CTest's time after a case's
limit. Cdb is a session driven over pipes: each command's output up to its
prompt, by a deadline.
"""

import hashlib
import os
import queue
import re
import threading
import time
from pathlib import Path

from foundation.devkit.python import answer
from foundation.devkit.python import process
from foundation.devkit.python import system

PINS = Path(__file__).resolve().with_name("debugger_pins.txt")
FRAMES = 32  # each stack's frames at most
HANG_THREADS = 16
HANG_LINES = 200  # the hang's lines at most, its first line among them
# The hang's debugger, found and run, within 12 s together: past them,
# process's ending (taskkill, then ENDING_SECONDS where an orphan holds cdb's
# pipe) and the watcher's 5 s for the case's stream make 27 s and taskkill's
# runs, inside the 30 s CTest leaves the watcher after a case's limit.
HANG_SECONDS = 12.0
VERSION_SECONDS = 30  # the debugger's -version, outside a hang
QUIT_SECONDS = 10  # a session's cdb, asked to quit, ending its case and itself
# A CI job: GitHub Actions sets GITHUB_ACTIONS to "true" in every step; the
# workflows set no variable of their own that names CI.
CI_VARIABLE = "GITHUB_ACTIONS"
CDB_VARIABLE = "QIVEN_CDB"  # names the pinned cdb.exe, as QIVEN_PYTHON the interpreter
SDK_CDB = Path("Debuggers", "x64", "cdb.exe")  # an SDK's cdb, under its root
CDB_VERSION = re.compile(r"\bversion (\d+(?:\.\d+)+)")
# A thread's header in cdb's ~*kn.
CDB_THREAD = re.compile(r"^[.#]?\s*\d+\s+Id: [0-9a-f]+\.[0-9a-f]+ ")
# cdb's lines that say nothing of the case.
NOISE = re.compile("|".join((
    r"^ModLoad: ", r"^\([0-9a-f]+\.[0-9a-f]+\): .* chance", r"^Breakpoint \d+ hit$",
    r"^\*\*\* WARNING: Unable to verify checksum", r"^NatVis script ", r"^quit:$")))
PROMPT = re.compile(r"(?:^|\n)\d+:\d+(?::x86)?> $")
QUIET = 0.05  # seconds of no output after a prompt before it is taken as one
EVENT = re.compile(r"^Last event: [0-9a-f]+\.[0-9a-f]+: (?P<event>.*)$", re.M)


class NotFound(Exception):
    """No debugger to use, the reason: one off its pin, one that cannot
    start, or Absent."""


class Absent(NotFound):
    """No debugger installed: "no cdb: ...", a skip."""


class TimedOut(Exception):
    """A session's deadline passed."""


def _pin(kind, name):
    """(version, digest, source) of the pin file's line KIND NAME, or None."""
    try:
        text = PINS.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        fields = line.split("#", 1)[0].split()
        if fields[:2] == [kind, name] and len(fields) == 5:
            return fields[2], fields[3], fields[4]
    return None


def _read(stream, chunks):
    """Puts what stream gives on the queue chunks, then None at its end."""
    while True:
        data = stream.read1(1 << 16)
        chunks.put(data or None)
        if not data:
            return


def _version(path, limit, shown):
    """The text path's -version gives up to its version line, within limit
    seconds, and the process ended there: a cdb can take seconds more to exit
    after that line. All the text where it ends with no such line; NotFound,
    the debugger named shown, where no line comes within limit."""
    deadline = time.monotonic() + limit
    try:
        ran = process.start([path, "-version"], err=process.MERGE)
    except OSError as e:
        raise NotFound(f"{shown} -version: {e}") from e
    chunks = queue.Queue()
    threading.Thread(target=_read, args=(ran.stdout, chunks), daemon=True).start()
    text = ""
    try:
        while not CDB_VERSION.search(text[:text.rfind("\n") + 1]):
            data = chunks.get(timeout=max(deadline - time.monotonic(), 0.0))
            if data is None:
                break
            text += data.decode("utf-8", "replace")
    except queue.Empty:
        raise NotFound(f"{shown} -version: {process.TimedOut([path], limit)}") from None
    finally:
        if ran.poll() is None:
            ran.end_tree()
    return text


def find(limit=VERSION_SECONDS):
    """The pinned cdb's path once its SHA-256 and its -version, run within
    limit seconds, hold: the file QIVEN_CDB names, else the SDK's. NotFound
    (Absent when neither exists)."""
    found = _pin("debugger", "cdb")
    if found is None:
        raise NotFound(f"no line 'debugger cdb' in {PINS.name}")
    version, digest, source = found
    named = os.environ.get(CDB_VARIABLE)
    if named:
        path = Path(named)
        if not path.is_file():
            raise NotFound(f"{CDB_VARIABLE} names {named}, no file")
    else:
        root = system.sdk_root()
        if root is None:
            raise Absent(f"no cdb: no {CDB_VARIABLE} and no Windows SDK, the registry naming no KitsRoot10")
        path = root / SDK_CDB
        if not path.is_file():
            raise Absent(f"no cdb: no {CDB_VARIABLE} and none in the SDK's {SDK_CDB.parent.as_posix()}")
    with open(path, "rb") as f:
        got = hashlib.file_digest(f, "sha256").hexdigest()
    if got != digest:
        raise NotFound(f"{path}'s SHA-256 is {got}; the pin {digest}, {source}, named by {CDB_VARIABLE}")
    text = _version(path, limit, path.name)
    if not re.search(rf"(?<![\w.]){re.escape(version)}(?![\w.])", text):
        raise NotFound(f"{path.name} -version gives {text.strip()[:80]!r}; the pin is {version}")
    return path


def _image_cdb(limit):
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
        found = root / SDK_CDB
        if not found.is_file():
            raise Absent(f"no cdb: none on PATH, none at {found.as_posix()}, the SDK's root by {how}")
    path = Path(found)
    text = _version(path, limit, path.as_posix())
    version = CDB_VERSION.search(text)
    if version is None:
        raise NotFound(f"{path.as_posix()} -version gives no version: {text.strip()[:80]!r}")
    return path, version.group(1)


def hang_debugger(limit=VERSION_SECONDS):
    """The hang's debugger: (path, how), how None for the pinned one
    (find()); in a CI job the image's cdb, how naming its version and path.
    Its -version runs within limit seconds."""
    if os.environ.get(CI_VARIABLE) == "true":
        path, version = _image_cdb(limit)
        return path, f"by the image's cdb {version} at {path.as_posix()}, unpinned (CI)"
    return find(limit), None


def module(binary):
    """cdb's name of a binary's module: its stem, each character other
    than a letter, a digit or _ as _."""
    return re.sub(r"[^A-Za-z0-9_]", "_", Path(binary).stem)


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
    symbols beside it, and its dump <binary>-<pid>-hang.dmp in dumps when it
    is given: (threads, lines, how), how hang_debugger()'s. The debugger is
    found and run within HANG_SECONDS together; past them, the lines it
    wrote. NotFound when no debugger is at hand or it cannot run."""
    deadline = time.monotonic() + HANG_SECONDS
    debugger, how = hang_debugger(HANG_SECONDS)
    dump = hang_dump(pid, binary, dumps)
    commands = f"~*kn {FRAMES}" + (f"; .dump /mt {dump}" if dump else "") + "; qd"
    argv = [debugger, "-pv", "-p", str(pid), "-sins", "-lines", "-y", Path(binary).parent, "-c", commands]
    try:
        done = process.run(argv, limit=max(deadline - time.monotonic(), 0.0))
    except process.TimedOut as e:
        done = e
    except OSError as e:
        raise NotFound(f"{Path(debugger).name}: {e}") from e
    return (*hang_lines(done.out.decode("utf-8", "replace") + done.err.decode("utf-8", "replace")), how)


class Cdb:
    """cdb over pipes, a session: each command's output up to its prompt,
    by the deadline (time.monotonic()), past which TimedOut. NotFound where
    cdb cannot start."""

    def __init__(self, exe, args, symbols, deadline, env=None, cwd=None):
        self.deadline = deadline
        argv = [exe, "-sins", "-lines", "-y", symbols, *args]
        try:
            self.process = process.start(argv, feed=True, env=env, cwd=cwd, err=process.MERGE)
        except OSError as e:
            raise NotFound(f"{Path(exe).name}: {e}") from e
        self.chunks = queue.Queue()
        threading.Thread(target=_read, args=(self.process.stdout, self.chunks), daemon=True).start()
        self.ended = False

    def wait(self):
        """The output up to the next prompt, its lines, the prompt and cdb's
        noise dropped."""
        text = ""
        while True:
            left = self.deadline - time.monotonic()
            if left <= 0:
                raise TimedOut()
            try:
                data = self.chunks.get(timeout=min(left, QUIET if PROMPT.search(text) else 0.5))
            except queue.Empty:
                if PROMPT.search(text):
                    break
                continue
            if data is None:
                self.ended = True
                break
            text += data.decode("utf-8", "replace").replace("\r\n", "\n")
        text = PROMPT.sub("", text)
        return [line for line in text.split("\n") if line.strip() and not NOISE.match(line)]

    def send(self, command):
        """The command's output lines; none once cdb has ended."""
        if self.ended:
            return []
        try:
            self.process.stdin.write(command.encode() + b"\n")
            self.process.stdin.flush()
        except OSError:
            self.ended = True
            return []
        return self.wait()

    def event(self):
        """The last event's text, as .lastevent gives it."""
        found = EVENT.search("\n".join(self.send(".lastevent")))
        return found["event"].strip() if found else ""

    def close(self, kill=False):
        """Quits, which ends the case; killed, past the time bound."""
        if self.process.poll() is None and not self.ended and not kill:
            try:
                self.process.stdin.write(b"q\n")
                self.process.stdin.flush()
                self.process.wait(QUIT_SECONDS)
            except (OSError, process.TimedOut):
                pass
        if self.process.poll() is None:
            self.process.end_tree()
