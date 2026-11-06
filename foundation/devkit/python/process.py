"""A program started, bounded and ended, for the repository's Python: the one
place subprocess stands above the system's back end, with no conditional on
the system. This directory's README states each member's call on each system.

find() finds a program. run() runs one to its end under a limit, past which
its whole tree is ended and TimedOut raised, what it wrote collected first:
Ran, its status and output. start() starts one and holds it: Process, its
streams, poll(), wait(limit) and end_tree().

Standard input is closed unless data is given, or feed asks for a pipe.
Output is bytes unless run() is asked for text. out and err take a pipe, the
default, a file open for it or INHERIT, the caller's own; err also MERGE,
into out. A word of argv is a str, a Path, a number or bytes; one str is a
whole command line, a compile database's. A program that cannot start raises
the system's OSError.
"""

import os
import shutil
import subprocess
from collections import namedtuple

from foundation.devkit.python import status as status_table
from foundation.devkit.python import system

_PIPE = subprocess.PIPE
MERGE = subprocess.STDOUT
INHERIT = None
LIMIT = 120  # seconds a run takes unless the caller says
_ENDING_SECONDS = 10  # seconds a program ended by force takes to end and its pipes to close


class Ran(namedtuple("Ran", ("status", "out", "err"))):
    """A finished run: its status, as subprocess gives it, and its output."""

    __slots__ = ()

    @property
    def named(self):
        """The status's line by the table."""
        return status_table.line(self.status)

    def __str__(self):
        tail = self.err[-2000:]
        return f"{self.named}; stderr: {tail if isinstance(tail, str) else tail.decode('utf-8', 'replace')}"


class TimedOut(Exception):
    """A limit passed: `limit`, and what a run's program wrote, `out` and
    `err`, empty where its pipes did not close."""

    def __init__(self, argv, limit, out=b"", err=b""):
        program = argv if isinstance(argv, str) else os.fsdecode(_words(argv)[0])
        super().__init__(f"process: {program} past its limit of {limit:g} s")
        self.limit, self.out, self.err = limit, out, err


class Process:
    """A program started and held: `pid`, and `stdin`, `stdout` and `stderr`
    where each is a pipe, else None."""

    def __init__(self, argv, child):
        self._argv, self._child = argv, child
        self.pid, self.stdin, self.stdout, self.stderr = child.pid, child.stdin, child.stdout, child.stderr

    def poll(self):
        """Its status, or None while it runs."""
        return self._child.poll()

    def wait(self, limit=LIMIT):
        """Its status once it ends; TimedOut past `limit`, which ends nothing."""
        try:
            return self._child.wait(timeout=limit)
        except subprocess.TimeoutExpired:
            raise TimedOut(self._argv, limit) from None

    def end_tree(self):
        """Ends it and every process it started, by force: its status, or None
        where it has not ended _ENDING_SECONDS later."""
        _end_tree(self._child)
        try:
            return self._child.wait(timeout=_ENDING_SECONDS)
        except subprocess.TimeoutExpired:
            return None


def find(name, path=None):
    """The file of the program `name`, as a str, on `path` (this process's PATH
    by default), or None."""
    return shutil.which(name, path=path)


def run(argv, *, data=None, env=None, cwd=None, limit=LIMIT, out=_PIPE, err=_PIPE, program=None, text=False):
    """Runs a program to its end: Ran. `data`, bytes, on its input. `program`
    the file to start where argv[0] is not it. `text`: its output as UTF-8
    text, what does not decode replaced, each line's ending \\n. Past `limit`
    seconds its tree is ended, then TimedOut."""
    child = _child(argv, subprocess.DEVNULL if data is None else _PIPE, out, err, env, cwd, program)
    try:
        written = child.communicate(data, timeout=limit)
    except subprocess.TimeoutExpired:
        _end_tree(child)
        try:
            written = child.communicate(timeout=_ENDING_SECONDS)
        except subprocess.TimeoutExpired:
            written = (None, None)  # a process the tree no longer reaches holds the pipe
        raise TimedOut(argv, limit, *_output(written, text)) from None
    except BaseException:
        child.kill()
        raise
    return Ran(child.returncode, *_output(written, text))


def start(argv, *, feed=False, env=None, cwd=None, out=_PIPE, err=_PIPE):
    """Starts a program and holds it: Process. `feed`: its input a pipe, the
    caller's to write and close."""
    return Process(argv, _child(argv, _PIPE if feed else subprocess.DEVNULL, out, err, env, cwd, None))


def _words(argv):
    """argv as subprocess takes it."""
    return argv if isinstance(argv, str) else [word if isinstance(word, bytes) else str(word) for word in argv]


def _child(argv, stdin, out, err, env, cwd, program):
    return subprocess.Popen(_words(argv), executable=None if program is None else str(program), stdin=stdin,
                            stdout=out, stderr=err, env=env, cwd=cwd)


def _end_tree(child):
    """Ends child's tree, then child itself, where the tree's means failed."""
    try:
        system.end_tree(child.pid)
    except system.Failed:
        pass  # the program itself is ended next, which needs no tree
    finally:
        child.kill()


def _output(written, text):
    """A run's (out, err), a stream not piped empty."""
    if text:
        return tuple((data or b"").decode("utf-8", "replace").replace("\r\n", "\n").replace("\r", "\n")
                     for data in written)
    return tuple(data or b"" for data in written)
