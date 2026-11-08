"""The call base: the one way devkit starts a foreign program, named, bounded,
its tree ended past its limit, its answer told apart as missing, slow, failed
or unreadable. See this directory's README.md.
"""

import os
import queue
import subprocess
import sys
import threading
import time
from collections import namedtuple
from collections.abc import Mapping

_ENDING_SECONDS = 10  # seconds the processes of a tree ended by force take to be gone
_LINES = 20  # lines kept of what a failed program said
_CHARACTERS = 4000  # characters kept of them, at most


class CallError(Exception):
    """A call that gave no answer: `call`, its name; `answer`, the word for
    what happened; `reason`, why."""

    answer = "error"

    def __init__(self, call, reason):
        super().__init__(f"{call}: {self.answer}: {reason}")
        self.call, self.reason = call, reason


class Missing(CallError):
    """No program where the caller said, or on the PATH it handed in."""

    answer = "missing"


class Slow(CallError):
    """The program started and passed its limit, `limit` seconds; its tree
    has been ended."""

    answer = "slow"

    def __init__(self, call, reason, limit):
        super().__init__(call, reason)
        self.limit = limit


class Failed(CallError):
    """The program ended with a status the call does not take, `status`, or
    could not start (status None)."""

    answer = "failed"

    def __init__(self, call, reason, status):
        super().__init__(call, reason)
        self.status = status


class Unreadable(CallError):
    """The program answered, but not in the form the call reads."""

    answer = "unreadable"


class Answer(namedtuple("Answer", ("name", "status", "out", "err"))):
    """A call's answer: its name, the program's status and its output, bytes."""

    __slots__ = ()


def locate(name, program, env):
    """The file `program` names: a path as it is, a bare name on the PATH of
    `env`, by its PATHEXT; Missing where there is none."""
    program = os.fspath(program)
    if os.path.dirname(program):
        if not os.path.isfile(program):
            raise Missing(name, f"no program at {program}")
        return os.path.abspath(program)
    path = _value(env, "PATH")
    if not path:
        raise Missing(name, f"{program}: the environment the call was given holds no PATH")
    extensions = [ext for ext in (_value(env, "PATHEXT") or "").split(os.pathsep) if ext]
    if not extensions or os.path.splitext(program)[1].upper() in (ext.upper() for ext in extensions):
        candidates = [program]
    else:
        candidates = [program + ext for ext in extensions]
    for directory in path.split(os.pathsep):
        for candidate in candidates:
            file = os.path.join(directory, candidate)
            if directory and os.path.isfile(file):
                return os.path.abspath(file)
    raise Missing(name, f"{program}: not on the PATH of the environment the call was given")


def call(name, program, args, *, env, cwd, limit, data=None, takes=(0,), said="utf-8"):
    """Runs `program` with `args` to its end under `limit` seconds: Answer
    where its status is one of `takes`, else Missing, Slow or Failed."""
    _check(name, env, cwd, limit)
    if data is not None and not isinstance(data, bytes):
        raise TypeError(f"{name}: data must be bytes, not {type(data).__name__}")
    from qiven_devkit.wrap import _job
    file = locate(name, program, env)
    job = _job.Job()
    try:
        child = _start(name, file, args, env, cwd, subprocess.DEVNULL if data is None else subprocess.PIPE,
                       subprocess.PIPE)
        _take(name, file, job, child)
        try:
            out, err = child.communicate(data, timeout=limit)
        except subprocess.TimeoutExpired:
            job.end()
            left = job.wait_empty(_ENDING_SECONDS)
            try:
                child.communicate(timeout=_ENDING_SECONDS)
            except subprocess.TimeoutExpired:
                pass  # the reason below names what is still alive
            reason = f"{file} passed its limit of {limit:g} s and its tree was ended"
            if left:
                reason += f"; {left} of its processes had not ended {_ENDING_SECONDS} s later"
            raise Slow(name, reason, limit) from None
        except BaseException:
            job.end()
            child.kill()
            raise
    finally:
        job.close()
    if child.returncode not in takes:
        raise Failed(name, f"{file} ended with status {_status(child.returncode)}: {_said(err, out, said)}",
                     child.returncode)
    return Answer(name, child.returncode, out, err)


def start(name, program, args, *, env, cwd, limit):
    """`program` with `args` started and held under `limit` seconds, for
    its whole life: a Running the caller talks to and closes."""
    _check(name, env, cwd, limit)
    from qiven_devkit.wrap import _job
    file = locate(name, program, env)
    job = _job.Job()
    try:
        child = _start(name, file, args, env, cwd, subprocess.PIPE, subprocess.STDOUT)
        _take(name, file, job, child)
    except BaseException:
        job.close()
        raise
    return Running(name, file, child, job, limit)


class Running:
    """A held program: `name`, `file`, `pid`, `limit`; send, read and wait
    within the limit, close ending its tree. See this directory's README.md."""

    def __init__(self, name, file, child, job, limit):
        self.name, self.file, self.pid, self.limit = name, file, child.pid, limit
        self._child, self._job = child, job
        self._deadline = time.monotonic() + limit
        self._chunks = queue.Queue()
        self._ended = False
        threading.Thread(target=_pump, args=(child.stdout, self._chunks), daemon=True).start()

    def __enter__(self):
        return self

    def __exit__(self, *raised):
        self.close()

    def send(self, data):
        """Writes the bytes data to the program's standard input."""
        if not isinstance(data, bytes):
            raise TypeError(f"{self.name}: data must be bytes, not {type(data).__name__}")
        self._left()
        try:
            self._child.stdin.write(data)
            self._child.stdin.flush()
        except OSError as error:
            raise Failed(self.name, f"{self.file} no longer reads its input: {error}", self._child.poll()) from None

    def read(self, seconds):
        """The next bytes the program wrote within `seconds`: b"" where none
        came, None once its output has ended."""
        if self._ended and self._job is not None:
            return None
        left = self._left()
        try:
            data = self._chunks.get(timeout=min(seconds, left))
        except queue.Empty:
            if time.monotonic() >= self._deadline:
                self._slow()
            return b""
        if data is None:
            self._ended = True
        return data

    def wait(self, seconds):
        """The program's status once it has ended; None where it still runs
        after `seconds`."""
        if self._job is not None and self._child.poll() is not None:
            return self._child.returncode
        left = self._left()
        try:
            return self._child.wait(timeout=min(seconds, left))
        except subprocess.TimeoutExpired:
            if time.monotonic() >= self._deadline:
                self._slow()
            return None

    def close(self):
        """Ends every process still in the job and lets go of it: the number
        of them still alive 10 s later."""
        if self._job is None:
            return 0
        try:
            self._job.end()
            left = self._job.wait_empty(_ENDING_SECONDS)
            try:
                self._child.stdin.close()
            except OSError:
                pass  # the program is gone; so is its end of the pipe
            try:
                self._child.wait(_ENDING_SECONDS)
            except subprocess.TimeoutExpired:
                pass  # the count above names what is still alive
        finally:
            self._job.close()
            self._job = None
        return left

    def _left(self):
        """Seconds left of the limit; Slow, the tree ended, where none are."""
        if self._job is None:
            raise ValueError(f"{self.name}: {self.file} has been closed")
        left = self._deadline - time.monotonic()
        if left <= 0:
            self._slow()
        return left

    def _slow(self):
        left = self.close()
        reason = f"{self.file} passed its limit of {self.limit:g} s and its tree was ended"
        if left:
            reason += f"; {left} of its processes had not ended {_ENDING_SECONDS} s later"
        raise Slow(self.name, reason, self.limit) from None


def _pump(stream, chunks):
    """Puts what `stream` gives on the queue `chunks`, then None at its end."""
    try:
        while data := stream.read1(1 << 16):
            chunks.put(data)
    except (OSError, ValueError):
        pass  # the pipe is broken or closed: its end, as for a reader
    finally:
        chunks.put(None)
        stream.close()


def _check(name, env, cwd, limit):
    """TypeError or ValueError where an argument every start takes is wrong;
    Failed where the system is not Windows."""
    if sys.platform != "win32":
        raise Failed(name, f"wrap ends a program's tree on Windows only; this system is {sys.platform}", None)
    if not isinstance(env, Mapping):
        raise TypeError(f"{name}: env must be a mapping, not {type(env).__name__}")
    if isinstance(limit, bool) or not isinstance(limit, (int, float)) or not limit > 0:
        raise ValueError(f"{name}: limit must be a number of seconds above 0, not {limit!r}")
    if cwd is None or not os.path.isdir(cwd):
        raise ValueError(f"{name}: cwd must be a directory, not {cwd!r}")


def _take(name, file, job, child):
    """The suspended child put into the job and run; Failed, the child
    ended, where it cannot be."""
    try:
        job.take(child.pid)
    except OSError as error:
        child.kill()
        child.communicate()
        raise Failed(name, f"{file} did not start: {error}", None) from None


def _start(name, file, args, env, cwd, stdin, stderr):
    """The program started suspended, with no window; Missing or Failed where
    it does not start."""
    from qiven_devkit.wrap import _job
    argv = [file, *(os.fspath(arg) if isinstance(arg, os.PathLike) else str(arg) for arg in args)]
    try:
        return subprocess.Popen(argv, stdin=stdin, stdout=subprocess.PIPE, stderr=stderr, env=dict(env), cwd=cwd,
                                creationflags=_job.CREATE_SUSPENDED | _job.CREATE_NO_WINDOW)
    except FileNotFoundError as error:
        raise Missing(name, f"{file}: {error}") from None
    except OSError as error:
        raise Failed(name, f"{file} did not start: {error}", None) from None


def _value(env, key):
    """The value of `key` in env, its case aside, as Windows reads it; None."""
    if key in env:
        return env[key]
    return next((value for name, value in env.items() if name.upper() == key), None)


def _status(status):
    """A status as a reader takes it: decimal, and hex where it is a system's code."""
    if status < 0 or status > 0x7FFFFFFF:
        return f"{status} (0x{status & 0xFFFFFFFF:08X})"
    return str(status)


def _said(err, out, codec):
    """The last lines a program wrote, of its error stream, else its output."""
    text = (err if err.strip() else out).decode(codec, "replace")
    lines = [line.rstrip() for line in text.splitlines() if line.strip()]
    if not lines:
        return "it wrote nothing"
    return " | ".join(lines[-_LINES:])[-_CHARACTERS:]
