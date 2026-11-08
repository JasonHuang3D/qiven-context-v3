"""cdb's wrapper: cdb found on the caller's pin by the variable QIVEN_CDB of
the environment it is handed, every thread's stack of a running process, and
a session on a program or a dump, cdb's text read here alone; its fakes. See
this directory's README.md.
"""

import hashlib
import os
import re
import time
from collections import namedtuple

from qiven_devkit.wrap import call
from qiven_devkit.wrap.fake import Fake

FIND, STACKS, SESSION, DUMP = "cdb find", "cdb stacks", "cdb session", "cdb dump"
BREAK, WATCH, RESUME, LOCALS = "cdb break", "cdb watch", "cdb resume", "cdb locals"
STACK, THREADS, EVALUATE, STEP = "cdb stack", "cdb threads", "cdb evaluate", "cdb step"
STEP_OVER, CONTEXT, DUMP_EVENT, QUIT = "cdb step over", "cdb exception context", "cdb dump event", "cdb quit"
VARIABLE = "QIVEN_CDB"  # names the cdb file in the environment the caller hands in

_MARK = "@cdb-stacks-end@"  # echoed after the stacks, before what follows them
_QUIT_SECONDS = 10  # cdb, asked to quit, ending its program and itself
_QUIET = 0.05  # seconds of no output after a prompt before it is taken as one
_PATIENCE = 0.5  # seconds a read waits while no prompt has come
_VERSION = re.compile(r"\bversion (\d+(?:\.\d+)+)")
_THREAD = re.compile(r"^[.#]?\s*\d+\s+Id: [0-9a-f]+\.[0-9a-f]+ ")  # a thread's line in ~*kn
_HEADS = re.compile(r"^\s*#\s+Child-SP\b")  # the columns' heads above a stack
_PROMPT = re.compile(r"(?:^|\n)\d+:\d+(?::x86)?> $")
_NOISE = re.compile("|".join((
    r"^ModLoad: ", r"^\([0-9a-f]+\.[0-9a-f]+\): .* chance", r"^Breakpoint \d+ hit$",
    r"^\*\*\* WARNING: Unable to verify checksum", r"^NatVis script ", r"^quit:$")))
_EVENT = re.compile(r"^Last event: [0-9a-f]+\.[0-9a-f]+: (?P<event>.*)$", re.M)
_EXIT = re.compile(r"^Exit process [0-9a-f]+:[0-9a-f]+, code (?P<code>[0-9a-f]+)$")
_CHANCE = re.compile(r"\s*\((?:first chance|!!! second chance !!!|first/second chance not available)\)$")
_SINGLE_STEP = "code 80000004 (first chance)"


class Pin(namedtuple("Pin", ("version", "sha256"))):
    """The caller's pin of cdb: the version its -version gives, and the
    SHA-256 of its file, in hex."""

    __slots__ = ()


class Stacks(namedtuple("Stacks", ("threads", "dump", "said"))):
    """A running process's stacks: `threads`, each a Thread; `dump`, the
    dump's path where it was written, else None; `said`, cdb's later lines."""

    __slots__ = ()


class Thread(namedtuple("Thread", ("header", "frames"))):
    """A thread's line as cdb wrote it, and the lines of its frames."""

    __slots__ = ()


class Stop(namedtuple("Stop", ("event", "lines", "exited", "fault", "own"))):
    """A session's stop: its event, its lines, the exit status where the
    process exited, whether it is a fault, whether it is the debugger's own
    single step."""

    __slots__ = ()


class OffPin(call.CallError):
    """The file QIVEN_CDB names is not the pin's: its digest or its version."""

    answer = "off its pin"


class NotAttached(call.CallError):
    """cdb could not attach to the process, and ran none of the commands."""

    answer = "not attached"


class Cdb:
    """cdb, the file the variable QIVEN_CDB of `env` names, checked on `pin`,
    run under `env`."""

    def __init__(self, env, pin):
        if not isinstance(pin, Pin):
            raise TypeError(f"{FIND}: pin must be a cdb.Pin, not {type(pin).__name__}")
        self.env, self.pin = env, pin
        self._found = None

    def find(self, limit):
        """The path of cdb once its file is on the pin; Missing, OffPin,
        Slow or Unreadable."""
        if self._found is None:
            self._found = self._find(FIND, limit)
        return self._found

    def stacks(self, pid, *, symbols, frames, limit, dump=None):
        """Stacks of every thread of the running process `pid`, its dump
        written to `dump` where given; NotAttached where cdb cannot attach."""
        _count(STACKS, "frames", frames)
        commands = f"~*kn {frames}; .echo {_MARK}; "
        if dump is not None:
            dump = os.fspath(dump)
            if not os.path.isabs(dump) or re.search(r'[\s;"]', dump):
                raise ValueError(f"{STACKS}: dump must be an absolute path with no whitespace, ';' or '\"', "
                                 f"not {dump!r}")
            commands += f".dump /mt {dump}; "
        deadline = time.monotonic() + _seconds(STACKS, limit)
        program = self._found or self._find(STACKS, limit)
        self._found = program
        if dump is not None and os.path.lexists(dump):
            os.remove(dump)
        args = ["-pv", "-p", str(int(pid)), *_options(STACKS, symbols), "-c", commands + "qd"]
        answer = call.call(STACKS, program, args, env=self.env, cwd=os.path.dirname(program),
                           limit=_left(STACKS, deadline, limit))
        lines = _lines(answer.out.decode("utf-8", "replace") + answer.err.decode("utf-8", "replace"))
        if _MARK not in lines:
            raise NotAttached(STACKS, f"cdb ran none of its commands on process {pid}: {_last(lines)}")
        at = lines.index(_MARK)
        threads = []
        for line in lines[:at]:
            if _THREAD.match(line):
                threads.append(Thread(line, []))
            elif threads and not _HEADS.match(line):
                threads[-1].frames.append(line)
        if not threads:
            raise call.Unreadable(STACKS, f"no thread's stack before the mark: {_last(lines[:at])}")
        return Stacks(threads, dump if dump is not None and os.path.isfile(dump) else None, lines[at + 1:])

    def session(self, program, args, *, symbols, cwd, limit):
        """A Session on `program` started under cdb with `args`, at its first
        stop, source lines on."""
        program = os.fspath(program)
        if not os.path.isabs(program):
            raise ValueError(f"{SESSION}: program must be an absolute path, not {program!r}")
        session = self._open(SESSION, [*_options(SESSION, symbols), program, *args], cwd, limit)
        with _closed_on_error(session):
            session._answer(SESSION)
            session._command(SESSION, ".lines -e")
            session._command(SESSION, "l+t")
        return session

    def dump(self, file, *, symbols, limit):
        """A Session on the dump `file`, its banner read."""
        file = os.fspath(file)
        if not os.path.isabs(file):
            raise ValueError(f"{DUMP}: file must be an absolute path, not {file!r}")
        session = self._open(DUMP, [*_options(DUMP, symbols), "-z", file], os.path.dirname(file), limit)
        with _closed_on_error(session):
            session._answer(DUMP)
        return session

    def _open(self, name, args, cwd, limit):
        deadline = time.monotonic() + _seconds(name, limit)
        program = self._found or self._find(name, limit)
        self._found = program
        return Session(call.start(name, program, args, env=self.env, cwd=cwd, limit=_left(name, deadline, limit)))

    def _find(self, name, limit):
        """The path of cdb on the pin, under the call name `name`."""
        pinned = f"the pin is version {self.pin.version}, SHA-256 {self.pin.sha256}"
        named = next((value for key, value in self.env.items() if key.upper() == VARIABLE), "")
        if not named:
            raise call.Missing(name, f"{VARIABLE} is not set in the environment the call was given; it names "
                                     f"the cdb file, and {pinned}")
        if not os.path.isabs(named) or not os.path.isfile(named):
            raise call.Missing(name, f"{VARIABLE} names {named}, which is not an absolute path to a file; "
                                     f"{pinned}")
        try:
            with open(named, "rb") as file:
                digest = hashlib.file_digest(file, "sha256").hexdigest()
        except OSError as error:
            raise call.Failed(name, f"{VARIABLE} names {named}, which cannot be read: {error}", None) from None
        if digest != self.pin.sha256.lower():
            raise OffPin(name, f"{VARIABLE} names {named}, whose SHA-256 is {digest}; the pin's is "
                               f"{self.pin.sha256}")
        answer = call.call(name, named, ["-version"], env=self.env, cwd=os.path.dirname(named), limit=limit)
        text = answer.out.decode("utf-8", "replace")
        found = _VERSION.search(text)
        if found is None:
            raise call.Unreadable(name, f"{named} -version gives no version: {_last(_lines(text))}")
        if found[1] != self.pin.version:
            raise OffPin(name, f"{VARIABLE} names {named}, whose version is {found[1]}; the pin's is "
                               f"{self.pin.version}")
        return named


class Session:
    """cdb held over its pipes: each method one command line, its answer read
    to cdb's prompt. See this directory's README.md."""

    def __init__(self, running):
        self.running = running
        self._go = "g"

    def __enter__(self):
        return self

    def __exit__(self, *raised):
        self.close()

    def break_at(self, spec, binary):
        """A breakpoint on a function or a file:line, in the module of
        `binary` unless `spec` names one: its lines."""
        named = spec if "!" in spec else f"{module(binary)}!{spec}"
        if re.fullmatch(r".+:\d+", spec) and not re.fullmatch(r"[A-Za-z]:\d+", spec):
            named = f"`{named}`"
        return self._command(BREAK, f"bp {_word(BREAK, named)}")

    def watch(self, expression):
        """A watch of writes to the 8 bytes of the object the C++ expression
        names: its lines."""
        return self._command(WATCH, f"ba w8 @@c++(&({_word(WATCH, expression)}))")

    def resume(self):
        """Runs on to the next stop: a Stop. A first-chance exception goes to
        the program's handlers unseen; a first-chance single step is the
        debugger's own, answered gh on the next resume."""
        while True:
            lines = self._command(RESUME, self._go)
            found = _EVENT.search("\n".join(self._command(RESUME, ".lastevent")))
            if found is None:
                raise call.Unreadable(RESUME, "cdb's .lastevent names no event")
            event = found["event"].strip()
            step = _SINGLE_STEP in event
            if "first chance" in event and not step:
                self._go = "gn"  # to the program's own handlers
                continue
            self._go = "gh" if step else "g"  # the debugger's own trap, never the program's
            return Stop(_CHANCE.sub("", event), lines, _exited(event), "second chance" in event, step)

    def dump_event(self):
        """A dump's event, a Stop with no lines: a fault where it has an
        exception's code and is no exit."""
        found = _EVENT.search("\n".join(self._command(DUMP_EVENT, ".lastevent")))
        if found is None:
            raise call.Unreadable(DUMP_EVENT, "cdb's .lastevent names no event")
        event = found["event"].strip()
        exited = _exited(event)
        return Stop(_CHANCE.sub("", event), [], exited, "code" in event and exited is None, False)

    def locals(self):
        return self._command(LOCALS, "dv /t /v")

    def stack(self, frames):
        return self._command(STACK, f"kn {_count(STACK, 'frames', frames)}")

    def threads(self, frames):
        return self._command(THREADS, f"~*kn {_count(THREADS, 'frames', frames)}")

    def evaluate(self, expression):
        return self._command(EVALUATE, f"?? {_word(EVALUATE, expression)}")

    def step(self, count):
        return self._command(STEP, f"t {_count(STEP, 'count', count)}")

    def step_over(self, count):
        return self._command(STEP_OVER, f"p {_count(STEP_OVER, 'count', count)}")

    def exception_context(self):
        return self._command(CONTEXT, ".ecxr")

    def quit(self):
        """Asks cdb to quit, which ends its program, waits for its end, and
        closes the session."""
        self.running.name = QUIT
        try:
            self.running.send(b"q\n")
            self.running.wait(_QUIT_SECONDS)
        except call.Failed:
            pass  # cdb had ended already: closing is all that is left
        finally:
            self.close()

    def close(self):
        """Ends cdb's tree, its program in it."""
        self.running.close()

    def _command(self, name, command):
        """The lines of cdb's answer to the command line `command`, its
        errors under the call name `name`."""
        self.running.name = name
        self.running.send(command.encode("utf-8") + b"\n")
        return self._answer(name)

    def _answer(self, name):
        """cdb's lines up to its next prompt; Failed where its output ends."""
        text = ""
        while True:
            data = self.running.read(_QUIET if _PROMPT.search(text) else _PATIENCE)
            if data is None:
                status = self.running.wait(_QUIT_SECONDS)
                lines = _lines(text)
                raise call.Failed(name, f"cdb ended in the session, with status {status}: {_last(lines)}", status)
            if not data:
                if _PROMPT.search(text):
                    return _lines(_PROMPT.sub("", text))
                continue
            text += data.decode("utf-8", "replace").replace("\r\n", "\n")


class FakeCdb(Fake):
    """cdb's fake: its methods, answered by call name; no cdb started."""

    def find(self, limit):
        return self._answer(FIND, limit=limit)

    def stacks(self, pid, *, symbols, frames, limit, dump=None):
        return self._answer(STACKS, pid=pid, symbols=symbols, frames=frames, limit=limit, dump=dump)

    def session(self, program, args, *, symbols, cwd, limit):
        return self._answer(SESSION, program=program, args=list(args), symbols=symbols, cwd=cwd, limit=limit)

    def dump(self, file, *, symbols, limit):
        return self._answer(DUMP, file=file, symbols=symbols, limit=limit)


class FakeSession(Fake):
    """A session's fake: its methods, answered by call name; close does
    nothing."""

    def __enter__(self):
        return self

    def __exit__(self, *raised):
        pass

    def break_at(self, spec, binary):
        return self._answer(BREAK, spec=spec, binary=binary)

    def watch(self, expression):
        return self._answer(WATCH, expression=expression)

    def resume(self):
        return self._answer(RESUME)

    def dump_event(self):
        return self._answer(DUMP_EVENT)

    def locals(self):
        return self._answer(LOCALS)

    def stack(self, frames):
        return self._answer(STACK, frames=frames)

    def threads(self, frames):
        return self._answer(THREADS, frames=frames)

    def evaluate(self, expression):
        return self._answer(EVALUATE, expression=expression)

    def step(self, count):
        return self._answer(STEP, count=count)

    def step_over(self, count):
        return self._answer(STEP_OVER, count=count)

    def exception_context(self):
        return self._answer(CONTEXT)

    def quit(self):
        return self._answer(QUIT)

    def close(self):
        pass


def module(binary):
    """cdb's name of a binary's module: its file's stem, each character
    other than a letter, a digit or _ as _."""
    stem = os.path.splitext(os.path.basename(os.fspath(binary)))[0]
    return re.sub(r"[^A-Za-z0-9_]", "_", stem)


class _closed_on_error:
    """Closes the session where the block raises, and lets the error go."""

    def __init__(self, session):
        self.session = session

    def __enter__(self):
        return self.session

    def __exit__(self, kind, error, trace):
        if kind is not None:
            self.session.close()


def _options(name, symbols):
    """The options every start of cdb takes."""
    folders = [os.fspath(folder) for folder in symbols]
    if any(";" in folder for folder in folders):
        raise ValueError(f"{name}: a symbols directory holds ';': {folders!r}")
    return ["-sins", "-lines", *(["-y", ";".join(folders)] if folders else [])]


def _word(name, text):
    """`text` as one part of one command line; ValueError where it would be
    more."""
    text = str(text)
    if re.search(r"[\r\n;]", text):
        raise ValueError(f"{name}: {text!r} holds a line break or ';', which cdb reads as another command")
    return text


def _count(name, what, number):
    """`number` where it is a whole number above 0."""
    if isinstance(number, bool) or not isinstance(number, int) or number < 1:
        raise ValueError(f"{name}: {what} must be a whole number above 0, not {number!r}")
    return number


def _seconds(name, limit):
    if isinstance(limit, bool) or not isinstance(limit, (int, float)) or not limit > 0:
        raise ValueError(f"{name}: limit must be a number of seconds above 0, not {limit!r}")
    return limit


def _left(name, deadline, limit):
    """Seconds left of the limit after cdb was found; Slow where none are."""
    left = deadline - time.monotonic()
    if left <= 0:
        raise call.Slow(name, f"finding cdb took the whole limit of {limit:g} s", limit)
    return left


def _exited(event):
    """The exit status an event gives, or None where it is no exit."""
    exited = _EXIT.match(event)
    return int(exited["code"], 16) if exited else None


def _lines(text):
    """cdb's lines, empty ones and its noise dropped."""
    return [line.rstrip() for line in text.replace("\r\n", "\n").split("\n")
            if line.strip() and not _NOISE.match(line)]


def _last(lines):
    """The last lines cdb wrote, for a reason."""
    return " | ".join(lines[-20:])[-4000:] or "it wrote nothing"
