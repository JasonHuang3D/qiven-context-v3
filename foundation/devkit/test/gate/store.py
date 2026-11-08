"""The store of one configuration, .local/states/<preset>/ under the checkout
(the test framework's design): its lock on one far byte, the holder at byte 0;
state.json, replaced whole by state.json.tmp, flushed, and os.replace; a run's
journal, runs/<run>.jsonl, and its files; the tree's id and the run's; the
retention of the runs' files; another configuration's state.json read for its
reds, without its lock. A file another process holds gets TRIES over 2 s.
This directory's README states each member.
"""

import json
import os
import re
import time
from pathlib import Path

from foundation.devkit.python import system

FORMAT = 1
LOCK_OFFSET = 1 << 30  # the locked byte, far past the holder's record
TRIES = 5
TRY_SECONDS = 0.5  # between two tries: 5 tries over 2 s
KEEP = 20  # the newest runs whose files retention keeps
_HOLDER_BYTES = 4096  # the most of the holder read
_TREE_ID = "states.id"
_RUN = re.compile(r"\d{8}T\d{6}Z-[0-9a-f]{4}")
_FIELDS = ("format", "preset", "tree", "code", "run", "build", "modules", "unowned", "tests")


class Internal(Exception):
    """The store's fault, the gate's INTERNAL."""


class Unreadable(Internal):
    """A store's file that cannot be read: its path and the reason."""


class WriteFailed(Internal):
    """A store's file that cannot be written: its path and the reason."""


class Locked(Exception):
    """Another run holds the store: its holder, a dictionary, empty where unread."""

    def __init__(self, place, holder):
        super().__init__(f"{place}: held by {json.dumps(holder) if holder else 'a run not yet named'}")
        self.holder = holder


def place(root, preset):
    """The store of the configuration `preset` under the checkout's root."""
    return Path(root) / ".local" / "states" / preset


class Lock:
    """The store's lock, held until release(); a `with` releases it."""

    def __init__(self, fd, path):
        self._fd, self.path = fd, path

    def release(self):
        if self._fd is not None:
            try:
                system.unlock_byte(self._fd, LOCK_OFFSET)
            finally:
                os.close(self._fd)
                self._fd = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.release()


def lock(where, argv):
    """The lock of the store at `where`, made where absent, its holder (this
    process's id, the start and `argv`) written at byte 0; Locked where another
    run holds it."""
    where = Path(where)
    path = where / "lock"
    try:
        where.mkdir(parents=True, exist_ok=True)
        fd = os.open(path, os.O_RDWR | os.O_CREAT | getattr(os, "O_BINARY", 0))
    except OSError as reason:
        raise WriteFailed(f"{path}: {reason}") from reason
    try:
        if not system.lock_byte(fd, LOCK_OFFSET):
            raise Locked(where, _holder(fd))
        holder = {"pid": os.getpid(), "start": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                  "argv": [str(word) for word in argv]}
        os.lseek(fd, 0, os.SEEK_SET)
        os.ftruncate(fd, 0)
        os.write(fd, (json.dumps(holder) + "\n").encode("utf-8"))
    except (OSError, system.Failed) as reason:
        os.close(fd)
        raise WriteFailed(f"{path}: {reason}") from reason
    except BaseException:
        os.close(fd)
        raise
    return Lock(fd, path)


def _holder(fd):
    """The holder written at byte 0, or {} where none can be read."""
    try:
        os.lseek(fd, 0, os.SEEK_SET)
        held = json.loads(os.read(fd, _HOLDER_BYTES).decode("utf-8").splitlines()[0])
        return held if isinstance(held, dict) else {}
    except (OSError, ValueError, IndexError):
        return {}


def empty(preset):
    """A new state.json's dictionary for `preset`."""
    return {"format": FORMAT, "preset": preset, "tree": None, "code": None, "run": None, "build": None,
            "modules": {}, "unowned": {}, "tests": {}}


def _tried(act, path, error):
    """act()'s answer, tried TRIES times TRY_SECONDS apart while it meets
    PermissionError; then `error`, naming path, the last reason."""
    for attempt in range(TRIES):
        try:
            return act()
        except PermissionError as reason:
            if attempt == TRIES - 1:
                raise error(f"{path}: held after {TRIES} tries: {reason}") from reason
            time.sleep(TRY_SECONDS)
        except OSError as reason:
            raise error(f"{path}: {reason}") from reason


def read(where):
    """The state.json of the store at `where`, or None where it has none;
    Unreadable where it cannot be read or is no state."""
    path = Path(where) / "state.json"
    if not path.exists():
        return None
    text = _tried(lambda: path.read_bytes(), path, Unreadable)
    try:
        state = json.loads(text.decode("utf-8"))
    except ValueError as reason:  # UnicodeDecodeError and JSONDecodeError are ValueErrors
        raise Unreadable(f"{path}: does not parse: {reason}") from reason
    if not isinstance(state, dict) or any(field not in state for field in _FIELDS) \
            or not isinstance(state["tests"], dict):
        raise Unreadable(f"{path}: is no state: it lacks a field of {', '.join(_FIELDS)}")
    return state


def write(where, state):
    """state written as the store's state.json, whole or not at all: by
    state.json.tmp, flushed and synced, then os.replace; WriteFailed where it
    cannot be."""
    path = Path(where) / "state.json"
    temporary = path.with_name("state.json.tmp")
    data = json.dumps({**state, "format": FORMAT}, indent=1, sort_keys=True).encode("utf-8") + b"\n"

    def put():
        with open(temporary, "wb") as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())

    _tried(put, temporary, WriteFailed)
    _tried(lambda: os.replace(temporary, path), path, WriteFailed)


def others(root, preset):
    """{preset: its state} of each other configuration's store under root
    that holds a state.json, read without its lock."""
    stores = Path(root) / ".local" / "states"
    found = {}
    for where in sorted(stores.iterdir()) if stores.is_dir() else ():
        if where.is_dir() and where.name != preset:
            state = read(where)
            if state is not None:
                found[where.name] = state
    return found


def tree_id(tree):
    """The tree's id, 16 hex digits, from its states.id, written from
    os.urandom where absent; Unreadable where it cannot be read or is none."""
    path = Path(tree) / _TREE_ID
    if not path.exists():
        made = os.urandom(8).hex()
        _tried(lambda: path.write_text(made + "\n", encoding="ascii"), path, WriteFailed)
        return made
    text = _tried(lambda: path.read_bytes(), path, Unreadable).decode("ascii", "replace").strip()
    if not re.fullmatch(r"[0-9a-f]{16}", text):
        raise Unreadable(f"{path}: is no tree id: {text[:40]!r}")
    return text


def run_id(now=None):
    """A run's id: <UTC yyyymmddThhmmssZ>-<4 hex>, at `now`, seconds since the
    epoch, or this time."""
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime(time.time() if now is None else now)) + "-" + \
        os.urandom(2).hex()


def spelled(run):
    """A run's id as every answer spells it."""
    return f"run:{run}"


def run_file(where, run, suffix):
    """The path of run's file of `suffix` (.jsonl, .log, .xml) in the store at `where`."""
    return Path(where) / "runs" / f"{run}{suffix}"


class Journal:
    """A run's journal, runs/<run>.jsonl: one JSON object a line, its kind
    first, each flushed as it is written. A `with` closes it."""

    def __init__(self, where, run):
        path = run_file(where, run, ".jsonl")
        path.parent.mkdir(parents=True, exist_ok=True)
        self.path = path
        try:
            self._out = open(path, "a", encoding="utf-8", newline="\n")
        except OSError as reason:
            raise WriteFailed(f"{path}: {reason}") from reason

    def write(self, kind, **fields):
        try:
            self._out.write(json.dumps({"kind": kind, **fields}) + "\n")
            self._out.flush()
        except OSError as reason:
            raise WriteFailed(f"{self.path}: {reason}") from reason

    def close(self):
        self._out.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def _runs(where):
    """{run: [its files]} of the store's runs/."""
    runs = {}
    folder = Path(where) / "runs"
    for path in sorted(folder.iterdir()) if folder.is_dir() else ():
        run = path.name.partition(".")[0]
        if _RUN.fullmatch(run):
            runs.setdefault(run, []).append(path)
    return runs


def unended(where):
    """The runs, in order, whose journal holds no end line: each killed or
    interrupted before its end."""
    found = []
    for run, paths in _runs(where).items():
        journal = run_file(where, run, ".jsonl")
        if journal in paths:
            lines = [line for line in _tried(lambda: journal.read_bytes(), journal, Unreadable).splitlines()
                     if line.strip()]
            try:
                ended = bool(lines) and json.loads(lines[-1]).get("kind") == "end"
            except (ValueError, AttributeError):
                ended = False
            if not ended:
                found.append(run)
    return found


def retain(where, cited):
    """The files of the newest KEEP runs and of each run in `cited` kept, every
    other run's removed: the lines of each file that could not be, for faults."""
    runs = _runs(where)
    kept = set(sorted(runs)[-KEEP:]) | {run.removeprefix("run:") for run in cited}
    faults = []
    for run in sorted(set(runs) - kept):
        for path in runs[run]:
            try:
                path.unlink()
            except OSError as reason:
                faults.append(f"{spelled(run)}: {path.name} cannot be removed: {reason}")
    return faults
