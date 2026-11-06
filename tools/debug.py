"""The debugger agents can drive: a case, a binary or a dump under cdb.exe,
with a bounded plain-text transcript.

    python -I tools/debug.py case <test> [--build <directory>] [actions] [bounds]
    python -I tools/debug.py run [actions] [bounds] -- <binary> [arguments]
    python -I tools/debug.py dump <file> [--symbols <directory>] [bounds]

- case runs a CTest test's command, read from the build's test list
  (ctest --show-only=json-v1; the build --build, else QIVEN_BUILD; ctest
  QIVEN_CTEST, else on PATH), the watcher in front of it dropped, with
  the test's environment and working directory; run runs a binary.
- Actions. Before the run: --break <function> (a bare name in the binary's
  module) or <file>:<line>, and --watch <expression>, a write watch of 8
  bytes on the object it names, set at the first stop. At each stop (a
  break, a watch, a single step or a fault), in this order: --locals,
  --stack, --threads (every thread's stack), --print <expression>, then
  --step <n> (into) or --next <n> (over), by source lines; then the run
  continues. A first-chance single step is the debugger's: a stop of its
  own, its line "stop <n>: Single step exception - code 80000004, the
  debugger's own, answered gh", whatever cdb's lines under it say of the
  program's handlers; never handed to the program and never a fault. The
  gh would also swallow a program's own first-chance single step, which
  no code of ours raises. Every other first-chance exception goes to the
  case's own handlers, unseen; its second chance is the fault, the last
  stop.
- A thread's create event falling on the resume from a stop makes the debug
  engine lose that stop's bookkeeping, on both cdbs: a data breakpoint's trap
  comes as that raw single step, its rip where the plain stop's stands, the
  write done; or the step off a break is lost, and the same breakpoint hits
  again at the same rip, each later stop one later.
- Bounds. At most --stops stops (5), --lines lines (200, at most 1,000),
  the last line among them, each at most 300 code points, cut with [cut],
  and --seconds seconds (60, at most 600), after which the debugger and
  the case are killed. Past the lines, the run goes on unseen and the last
  line but one is [cut]. The banner, prompts, module loads and checksum
  warnings are dropped; each action is echoed as "> <action>".
- The last line: "debug: <outcome>, <stops> stops, <lines> lines,
  <seconds> s", the outcome "exited <status> <name>" by
  foundation/devkit/python/status.py's table, "fault <cause>", "stop
  bound", "timed out", or "could not run:
  <reason>". The exit code is 0 when the run ended as asked (exited, or
  the stop bound), 1 when the case faulted or the time bound passed, 2
  when it could not run (no debugger, a debugger off its pin, no binary,
  no dump, a bad argument).
- dump opens a minidump: the event, the exception's context (.ecxr), its
  stack and locals, and every thread's stack, within the same bounds; a
  dump of a fault ends "fault <cause>", exit 1; one of none, "dump read",
  exit 0. Its binaries are found in --symbols, by default the minidump's
  directory.

Linux's wrapper, tools/debug_gdb.py, is retired (tools/README.md's Retired
section). The debugger is found, checked and driven by
foundation/devkit/python/debugger.py.
"""

import argparse
import os
import re
import sys
import time
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent))  # under -I the checkout is off sys.path; appended, so that the standard library still wins
from foundation.devkit.python import answer
from foundation.devkit.python import build_tree
from foundation.devkit.python import debugger
from foundation.devkit.python import process
from foundation.devkit.python import status as status_table
from foundation.devkit.python.debugger import FRAMES

EXIT = re.compile(r"^Exit process [0-9a-f]+:[0-9a-f]+, code (?P<code>[0-9a-f]+)$")
CHANCE = re.compile(r"\s*\((?:first chance|!!! second chance !!!|first/second chance not available)\)$")
SINGLE_STEP = "code 80000004 (first chance)"
WATCHER = "watch.py"


class CannotRun(Exception):
    """The run could not start: exit 2."""


class Transcript:
    """The lines, within the bound: the last line always has its place,
    and the line before it is [cut] once a line was dropped."""

    def __init__(self, bound):
        self.bound, self.lines, self.stops = bound, [], 0

    def add(self, line):
        if len(self.lines) < self.bound:  # one past the room before the last line, so that a cut shows
            self.lines.append(answer.clip(line))

    def write(self, last, out):
        """Prints the lines, then last(the count of every line printed)."""
        lines = answer.within(self.lines, self.bound - 1)
        lines.append(answer.clip(last(len(lines) + 1)))
        out.write("".join(line + "\n" for line in lines))
        out.flush()


def break_spec(spec, binary):
    """cdb's breakpoint expression for a function or a file:line, in the
    binary's module unless spec names one: a bare file:line stays deferred on
    the latest cdb and never resolves."""
    named = spec if "!" in spec else f"{debugger.module(binary)}!{spec}"
    if re.fullmatch(r".+:\d+", spec) and not re.fullmatch(r"[A-Za-z]:\d+", spec):
        return f"`{named}`"
    return named


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
        found = status_table.named(status)
        return f"exited {status_table.value_text(status)} {found[0] if found else status_table.UNNAMED}", 0
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
        step = SINGLE_STEP in event
        if "first chance" in event and not step:
            go = "gn"  # to the case's own handlers
            continue
        go = "gh" if step else "g"  # the debugger's own trap, never the case's
        final = outcome_of(event)
        if final and final[1] == 0:
            return final
        out.stops += 1
        own = ", the debugger's own, answered gh" if step else ""  # not the program's, whatever cdb's lines below say
        out.add(f"stop {out.stops}: {CHANCE.sub('', event)}{own}")
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
    """Whether the file is a minidump."""
    try:
        with open(path, "rb") as f:
            head = f.read(4)
    except OSError:
        return False
    return head == b"MDMP"


def ctest_command(test, build):
    """The test's (command, environment, working directory), the watcher
    in front of it dropped, from the build's test list."""
    try:
        found = build_tree.tests(build).get(test)
    except build_tree.ListingFailed as e:
        raise CannotRun(f"ctest's test list: {e}") from e
    if not found or not found.command:
        raise CannotRun(f"the build lists no test {test}")
    command = [str(c) for c in found.command]
    if any(Path(c).name == WATCHER for c in command) and "--" in command:
        command = command[command.index("--") + 1:]
    env = dict(os.environ)
    env.update(v.split("=", 1) for v in found.properties.get("ENVIRONMENT", ()) if "=" in v)
    return command, env, found.properties.get("WORKING_DIRECTORY")


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
    shapes["case"].add_argument("--build", help="the build directory (default: QIVEN_BUILD)")
    shapes["dump"].add_argument("file")
    shapes["dump"].add_argument("--symbols", help="the binaries' directory (default: the dump's)")
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
            raise CannotRun(f"{args.file} is no minidump")
        symbols = Path(args.symbols) if args.symbols else None
        return [str(Path(args.file).resolve())], symbols, None, None, None
    env, cwd = None, None
    if args.mode == "case":
        build = args.build or os.environ.get("QIVEN_BUILD")
        if not build:
            raise CannotRun("no build: give --build or set QIVEN_BUILD")
        command, env, cwd = ctest_command(args.test, build)
    else:
        command = args.command
    binary = process.find(command[0]) or command[0]
    if not Path(binary).is_file():
        raise CannotRun(f"no binary {command[0]}")
    return [str(Path(binary).resolve()), *command[1:]], Path(binary).resolve().parent, Path(binary), env, cwd


def under_cdb(args, out, deadline):
    """The run, or the dump, under cdb: (outcome, code)."""
    exe = debugger.find()
    cdb_args, symbols, binary, env, cwd = target(args)
    if args.mode == "dump":
        symbols = symbols or Path(cdb_args[0]).parent
        cdb_args = ["-z", *cdb_args]
    cdb = debugger.Cdb(exe, cdb_args, symbols, deadline, env, cwd)
    try:
        return dump_session(cdb, out) if args.mode == "dump" else run_session(cdb, args, binary, out)
    except debugger.TimedOut:
        return "timed out", 1
    finally:
        cdb.close(kill=time.monotonic() >= deadline)


def main(argv=None):
    start = time.monotonic()
    out = Transcript(200)
    try:
        args = parse(sys.argv[1:] if argv is None else argv)
        out.bound = args.lines
        outcome, code = under_cdb(args, out, start + args.seconds)
    except (CannotRun, debugger.NotFound) as e:
        outcome, code = f"could not run: {e}", 2
    seconds = f"{time.monotonic() - start:.1f}"
    out.write(lambda lines: f"debug: {outcome}, {out.stops} stops, {lines} lines, {seconds} s", sys.stdout)
    return code


if __name__ == "__main__":
    sys.exit(main())
