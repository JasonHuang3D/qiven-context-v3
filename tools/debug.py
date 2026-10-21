"""The debugger agents can drive: a case, a binary or a dump under cdb.exe
(Windows) or gdb (Linux), with a bounded plain-text transcript.

    python -I tools/debug.py case <test> [--build <directory>] [actions] [bounds]
    python -I tools/debug.py run [actions] [bounds] -- <binary> [arguments]
    python -I tools/debug.py dump <file> [--symbols <directory>] [bounds]

- case runs a CTest test's command, read from the build's test list
  (ctest --show-only=json-v1; the build --build, else QIVEN3_BUILD; ctest
  QIVEN3_CTEST, else on PATH), the watcher in front of it dropped, with
  the test's environment and working directory; run runs a binary.
- Actions. Before the run: --break <function> (a bare name in the binary's
  module) or <file>:<line>, and --watch <expression>, a write watch of 8
  bytes on the object it names, set at the first stop. At each stop (a
  break, a watch or a fault), in this order: --locals, --stack, --threads
  (every thread's stack), --print <expression>, then --step <n> (into) or
  --next <n> (over), by source lines; then the run continues. A
  first-chance exception goes to the case's own handlers, unseen; its
  second chance is the fault, the last stop.
- Bounds. At most --stops stops (5), --lines lines (200, at most 1,000),
  the last line among them, each at most 300 code points, cut with [cut],
  and --seconds seconds (60, at most 600), after which the debugger and
  the case are killed. Past the lines, the run goes on unseen and the last
  line but one is [cut]. The banner, prompts, module loads and checksum
  warnings are dropped; each action is echoed as "> <action>".
- The last line: "debug: <outcome>, <stops> stops, <lines> lines,
  <seconds> s", the outcome "exited <status> <name>" by tools/status.py's
  table, "fault <cause>", "stop bound", "timed out", or "could not run:
  <reason>". The exit code is 0 when the run ended as asked (exited, or
  the stop bound), 1 when the case faulted or the time bound passed, 2
  when it could not run (no debugger, a debugger off its pin, no binary,
  no dump, a bad argument).
- dump opens a minidump (Windows) or a core file (Linux): the event, the
  exception's context (.ecxr; gdb's frame), its stack and locals, and
  every thread's stack, within the same bounds; a dump of a fault ends
  "fault <cause>", exit 1; one of none, "dump read", exit 0. Its binaries
  are found in --symbols, by default the minidump's directory on Windows,
  and on Linux the binary the core's notes name, where it ran.

On Linux the wrapper is gdb's, in batch mode, tools/debug_gdb.py: the same
actions, bounds, transcript and exit codes; a signal is the fault, seen
before the case's own handlers. The debugger is found and checked by
tools/debuggers.py.
"""

import argparse
import json
import os
import queue
import re
import runpy
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATUS = runpy.run_path(str(ROOT / "tools" / "status.py"))  # under -I this directory is off sys.path
DEBUGGERS = runpy.run_path(str(ROOT / "tools" / "debuggers.py"))
GDB = runpy.run_path(str(ROOT / "tools" / "debug_gdb.py"))
WINDOWS = sys.platform == "win32"
FRAMES = DEBUGGERS["FRAMES"]
CUT = DEBUGGERS["CUT"]
PROMPT = re.compile(r"(?:^|\n)\d+:\d+(?::x86)?> $")
QUIET = 0.05  # seconds of no output after a prompt before it is taken as one
EVENT = re.compile(r"^Last event: [0-9a-f]+\.[0-9a-f]+: (?P<event>.*)$", re.M)
EXIT = re.compile(r"^Exit process [0-9a-f]+:[0-9a-f]+, code (?P<code>[0-9a-f]+)$")
CHANCE = re.compile(r"\s*\((?:first chance|!!! second chance !!!|first/second chance not available)\)$")
WATCHER = "watch.py"


class CannotRun(Exception):
    """The run could not start: exit 2."""


class TimedOut(Exception):
    """The time bound passed."""


class Transcript:
    """The lines, within the bound: the last line always has its place,
    and the line before it is [cut] once a line was dropped."""

    def __init__(self, bound):
        self.bound, self.lines, self.cut, self.stops = bound, [], False, 0

    def add(self, line):
        if len(self.lines) < self.bound - 1:
            self.lines.append(DEBUGGERS["clip"](line))
        else:
            self.cut = True

    def write(self, last, out):
        """Prints the lines, then last(the count of every line printed)."""
        lines = self.lines[:-1] + [CUT] if self.cut else self.lines
        lines.append(DEBUGGERS["clip"](last(len(lines) + 1)))
        out.write("".join(line + "\n" for line in lines))
        out.flush()


class Cdb:
    """cdb over pipes: each command's output up to its prompt."""

    def __init__(self, exe, args, symbols, deadline, env=None, cwd=None):
        self.deadline = deadline
        argv = [str(exe), "-sins", "-lines", "-y", str(symbols), *args]
        try:
            self.process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, env=env, cwd=cwd)
        except OSError as e:
            raise CannotRun(f"{Path(exe).name}: {e}") from e
        self.chunks = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()
        self.ended = False

    def _read(self):
        while True:
            data = self.process.stdout.read1(1 << 16)
            self.chunks.put(data or None)
            if not data:
                return

    def wait(self):
        """The output up to the next prompt, its lines, the prompt dropped."""
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
        return [line for line in text.split("\n") if line.strip() and not DEBUGGERS["NOISE"].match(line)]

    def send(self, command):
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
                self.process.wait(timeout=10)
            except (OSError, subprocess.TimeoutExpired):
                pass
        if self.process.poll() is None:
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(self.process.pid)], capture_output=True,
                           stdin=subprocess.DEVNULL, timeout=30, check=False)
            self.process.kill()
        self.process.wait()


def break_spec(spec, binary):
    """cdb's breakpoint expression for a function or a file:line."""
    if re.fullmatch(r".+:\d+", spec) and not re.fullmatch(r"[A-Za-z]:\d+", spec):
        return f"`{spec}`"
    return spec if "!" in spec else f"{DEBUGGERS['module'](binary)}!{spec}"


def stop_actions(args):
    """(echo, command) of each action at a stop, in the text's order."""
    out = []
    if args.locals:
        out.append(("locals", "dv /t /v"))
    if args.stack:
        out.append(("stack", f"kn {FRAMES}"))
    if args.threads:
        out.append(("threads", f"~*kn {FRAMES}"))
    out += [(f"print {e}", f"?? {e}") for e in args.print]
    if args.step:
        out.append((f"step {args.step}", f"t {args.step}"))
    if args.next:
        out.append((f"next {args.next}", f"p {args.next}"))
    return out


def outcome_of(event):
    """(outcome, exit code) of a final event, or None for a stop the run
    goes on from."""
    exited = EXIT.match(event)
    if exited:
        status = int(exited["code"], 16)
        found = STATUS["named"](status)
        return f"exited {STATUS['value_text'](status)} {found[0] if found else STATUS['UNNAMED']}", 0
    if "second chance" in event:
        return f"fault {CHANCE.sub('', event)}", 1
    return None


def run_session(cdb, args, binary, out):
    """Runs the case under cdb: (outcome, code), the stops counted in out."""
    cdb.wait()  # the banner, to the initial break
    for setup in (".lines -e", "l+t"):
        cdb.send(setup)
    for spec in args.break_:
        out.add(f"> break {spec}")
        for line in cdb.send(f"bp {break_spec(spec, binary)}"):
            out.add(line)
    go = "g"
    while True:
        lines = cdb.send(go)
        event = cdb.event()
        if cdb.ended or not event:
            return f"could not run: {binary.name}: the debugger ended", 2
        if "first chance" in event:
            go = "gn"  # to the case's own handlers
            continue
        go = "g"
        final = outcome_of(event)
        if final and final[1] == 0:
            return final
        out.stops += 1
        out.add(f"stop {out.stops}: {CHANCE.sub('', event)}")
        for line in lines:
            out.add(line)
        if out.stops == 1:
            for expression in args.watch:
                out.add(f"> watch {expression}")
                for line in cdb.send(f"ba w8 @@c++(&({expression}))"):
                    out.add(line)
        for echo, command in stop_actions(args):
            out.add(f"> {echo}")
            for line in cdb.send(command):
                out.add(line)
        if final:
            return final
        if out.stops >= args.stops:
            return "stop bound", 0


def dump_session(cdb, out):
    """Reads a dump under cdb: (outcome, code)."""
    cdb.wait()
    event = cdb.event()
    out.add(f"dump: {CHANCE.sub('', event) or 'no event'}")
    for echo, command in (("context", ".ecxr"), ("stack", f"kn {FRAMES}"), ("locals", "dv /t /v"),
                          ("threads", f"~*kn {FRAMES}")):
        out.add(f"> {echo}")
        for line in cdb.send(command):
            out.add(line)
    if "code" in event and not EXIT.match(event):
        return f"fault {CHANCE.sub('', event)}", 1
    return "dump read", 0


def is_dump(path):
    """Whether the file is a minidump (Windows) or a core file (Linux)."""
    try:
        with open(path, "rb") as f:
            head = f.read(18)
    except OSError:
        return False
    if WINDOWS:
        return head[:4] == b"MDMP"
    return head[:4] == b"\x7fELF" and len(head) == 18 and int.from_bytes(head[16:18], "little") == 4


def ctest_command(test, build):
    """The test's (command, environment, working directory), the watcher
    in front of it dropped, from the build's test list."""
    ctest = os.environ.get("QIVEN3_CTEST") or shutil.which("ctest")
    if not ctest:
        raise CannotRun("no ctest: set QIVEN3_CTEST or put it on PATH")
    try:
        done = subprocess.run([ctest, "--show-only=json-v1", "--test-dir", str(build)], capture_output=True,
                              stdin=subprocess.DEVNULL, timeout=120, check=False)
        tests = json.loads(done.stdout.decode("utf-8", "replace"))["tests"]
    except (OSError, subprocess.TimeoutExpired, ValueError, KeyError) as e:
        raise CannotRun(f"ctest's test list: {e}") from e
    found = [t for t in tests if t.get("name") == test]
    if not found or not found[0].get("command"):
        raise CannotRun(f"the build lists no test {test}")
    command = [str(c) for c in found[0]["command"]]
    if any(Path(c).name == WATCHER for c in command) and "--" in command:
        command = command[command.index("--") + 1:]
    env, cwd = dict(os.environ), None
    for prop in found[0].get("properties", ()):
        if prop.get("name") == "ENVIRONMENT":
            env.update(v.split("=", 1) for v in prop.get("value", ()) if "=" in v)
        elif prop.get("name") == "WORKING_DIRECTORY":
            cwd = prop.get("value")
    return command, env, cwd


def bounded(low, high):
    def value(text):
        number = int(text)
        if not low <= number <= high:
            raise argparse.ArgumentTypeError(f"{number} is not within {low} to {high}")
        return number
    return value


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise CannotRun(f"bad argument: {message}")


def parse(argv):
    parser = Parser(prog="debug.py", description="Runs a case, a binary or a dump under the debugger.")
    commands = parser.add_subparsers(dest="mode", required=True)
    shapes = {"case": commands.add_parser("case"), "run": commands.add_parser("run"),
              "dump": commands.add_parser("dump")}
    shapes["case"].add_argument("test")
    shapes["case"].add_argument("--build", help="the build directory (default: QIVEN3_BUILD)")
    shapes["dump"].add_argument("file")
    shapes["dump"].add_argument("--symbols", help="the binaries' directory (default: the dump's on Windows; "
                                "on Linux the binary the core's notes name)")
    for name in ("case", "run"):
        shape = shapes[name]
        shape.add_argument("--break", dest="break_", action="append", default=[])
        shape.add_argument("--watch", action="append", default=[])
        for flag in ("--locals", "--stack", "--threads"):
            shape.add_argument(flag, action="store_true")
        shape.add_argument("--print", action="append", default=[])
        steps = shape.add_mutually_exclusive_group()
        steps.add_argument("--step", type=bounded(1, 1000))
        steps.add_argument("--next", type=bounded(1, 1000))
    for shape in shapes.values():
        shape.add_argument("--stops", type=bounded(1, 1000), default=5)
        shape.add_argument("--lines", type=bounded(2, 1000), default=200)
        shape.add_argument("--seconds", type=bounded(1, 600), default=60)
    if "--" in argv:
        at = argv.index("--")
        argv, command = argv[:at], argv[at + 1:]
    else:
        command = []
    args = parser.parse_args(argv)
    args.command = command
    if args.mode == "run" and not command:
        parser.error("run needs -- <binary> [arguments]")
    if args.mode != "run" and command:
        parser.error(f"{args.mode} takes no -- <binary>")
    return args


def target(args):
    """(the command, or the dump, symbols' directory, the binary, env, cwd)."""
    if args.mode == "dump":
        if not is_dump(args.file):
            raise CannotRun(f"{args.file} is no {'minidump' if WINDOWS else 'core file'}")
        symbols = Path(args.symbols) if args.symbols else None
        return [str(Path(args.file).resolve())], symbols, None, None, None
    env, cwd = None, None
    if args.mode == "case":
        build = args.build or os.environ.get("QIVEN3_BUILD")
        if not build:
            raise CannotRun("no build: give --build or set QIVEN3_BUILD")
        command, env, cwd = ctest_command(args.test, build)
    else:
        command = args.command
    binary = shutil.which(command[0], path=os.environ.get("PATH")) or command[0]
    if not Path(binary).is_file():
        raise CannotRun(f"no binary {command[0]}")
    return [str(Path(binary).resolve()), *command[1:]], Path(binary).resolve().parent, Path(binary), env, cwd


def under_cdb(args, out, deadline):
    """The run, or the dump, under cdb: (outcome, code)."""
    exe = DEBUGGERS["find_cdb"]()
    cdb_args, symbols, binary, env, cwd = target(args)
    if args.mode == "dump":
        symbols = symbols or Path(cdb_args[0]).parent
        cdb_args = ["-z", *cdb_args]
    cdb = Cdb(exe, cdb_args, symbols, deadline, env, cwd)
    try:
        return dump_session(cdb, out) if args.mode == "dump" else run_session(cdb, args, binary, out)
    except TimedOut:
        return "timed out", 1
    finally:
        cdb.close(kill=time.monotonic() >= deadline)


def under_gdb(args, out, deadline):
    """The run, or the core, under gdb: (outcome, code)."""
    gdb = DEBUGGERS["find_gdb"]()
    command, symbols, _, env, cwd = target(args)
    shared = {"FRAMES": FRAMES, "NOISE": DEBUGGERS["NOISE"], "STATUS": STATUS}
    if args.mode == "dump":
        return GDB["dump"](gdb, command[0], symbols, out, deadline, shared)
    return GDB["run"](gdb, args, command, env, cwd, out, deadline, shared)


def main(argv=None):
    start = time.monotonic()
    out = Transcript(200)
    try:
        args = parse(sys.argv[1:] if argv is None else argv)
        out.bound = args.lines
        outcome, code = (under_cdb if WINDOWS else under_gdb)(args, out, start + args.seconds)
    except (CannotRun, DEBUGGERS["NotFound"]) as e:
        outcome, code = f"could not run: {e}", 2
    seconds = f"{time.monotonic() - start:.1f}"
    out.write(lambda lines: f"debug: {outcome}, {out.stops} stops, {lines} lines, {seconds} s", sys.stdout)
    return code


if __name__ == "__main__":
    sys.exit(main())
