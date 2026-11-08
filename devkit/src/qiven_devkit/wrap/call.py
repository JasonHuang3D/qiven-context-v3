"""The call base: the one way devkit starts a foreign program, named, bounded,
its tree ended past its limit, its answer told apart as missing, slow, failed
or unreadable. See this directory's README.md.
"""

import os
import subprocess
import sys
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
    if sys.platform != "win32":
        raise Failed(name, f"wrap ends a program's tree on Windows only; this system is {sys.platform}", None)
    if not isinstance(env, Mapping):
        raise TypeError(f"{name}: env must be a mapping, not {type(env).__name__}")
    if isinstance(limit, bool) or not isinstance(limit, (int, float)) or not limit > 0:
        raise ValueError(f"{name}: limit must be a number of seconds above 0, not {limit!r}")
    if data is not None and not isinstance(data, bytes):
        raise TypeError(f"{name}: data must be bytes, not {type(data).__name__}")
    if cwd is None or not os.path.isdir(cwd):
        raise ValueError(f"{name}: cwd must be a directory, not {cwd!r}")
    from qiven_devkit.wrap import _job
    file = locate(name, program, env)
    job = _job.Job()
    try:
        child = _start(name, file, args, env, cwd, data)
        try:
            job.take(child.pid)
        except OSError as error:
            child.kill()
            child.communicate()
            raise Failed(name, f"{file} did not start: {error}", None) from None
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


def _start(name, file, args, env, cwd, data):
    """The program started suspended, with no window; Missing or Failed where
    it does not start."""
    from qiven_devkit.wrap import _job
    argv = [file, *(os.fspath(arg) if isinstance(arg, os.PathLike) else str(arg) for arg in args)]
    try:
        return subprocess.Popen(argv, stdin=subprocess.DEVNULL if data is None else subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=dict(env), cwd=cwd,
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
