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
- dump opens a minidump: the event, the exception's context, its
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
import sys
import time
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent))  # under -I the checkout is off sys.path; appended, so that the standard library still wins
from foundation.devkit.python import answer
from foundation.devkit.python import build_tree
from foundation.devkit.python import debugger
from foundation.devkit.python import process
from foundation.devkit.python import status as status_table

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


def act(out, echo, call, *arguments):
    """One action of the session: its echo, then its lines."""
    out.add(f"> {echo}")
    for line in call(*arguments):
        out.add(line)


def stop_actions(session, args, out):
    """The actions at a stop, in the text's order."""
    if args.locals:
        act(out, "locals", session.locals)
    if args.stack:
        act(out, "stack", session.stack)
    if args.threads:
        act(out, "threads", session.threads)
    for expression in args.print:
        act(out, f"print {expression}", session.evaluate, expression)
    if args.step:
        act(out, f"step {args.step}", session.step, args.step)
    if args.next:
        act(out, f"next {args.next}", session.step_over, args.next)


def exited(status):
    """The outcome of a process's exit."""
    found = status_table.named(status)
    return f"exited {status_table.value_text(status)} {found[0] if found else status_table.UNNAMED}"


def run_session(session, args, binary, out):
    """Runs the case in the session: (outcome, code), the stops counted in
    out."""
    session.begin()
    for spec in args.break_:
        act(out, f"break {spec}", session.set_break, spec, binary)
    while True:
        stop = session.resume()
        if stop is None:
            return f"could not run: {binary.name}: the debugger ended", 2
        if stop.status is not None:
            return exited(stop.status), 0
        out.stops += 1
        out.add(f"stop {out.stops}: {stop.text}")
        for line in stop.lines:
            out.add(line)
        if out.stops == 1:
            for expression in args.watch:
                act(out, f"watch {expression}", session.watch, expression)
        stop_actions(session, args, out)
        if stop.fault:
            return f"fault {stop.text}", 1
        if out.stops >= args.stops:
            return "stop bound", 0


def dump_session(session, out):
    """Reads a dump in the session: (outcome, code)."""
    stop = session.dump_stop()
    out.add(f"dump: {stop.text or 'no event'}")
    act(out, "context", session.context)
    act(out, "stack", session.stack)
    act(out, "locals", session.locals)
    act(out, "threads", session.threads)
    if stop.fault:
        return f"fault {stop.text}", 1
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


def under_debugger(args, out, deadline):
    """The run, or the dump, under the debugger: (outcome, code)."""
    exe = debugger.find()
    command, symbols, binary, env, cwd = target(args)
    if args.mode == "dump":
        session = debugger.Cdb.on_dump(exe, command[0], symbols or Path(command[0]).parent, deadline)
    else:
        session = debugger.Cdb(exe, command, symbols, deadline, env, cwd)
    try:
        return dump_session(session, out) if args.mode == "dump" else run_session(session, args, binary, out)
    except debugger.TimedOut:
        return "timed out", 1
    finally:
        session.close(kill=time.monotonic() >= deadline)


def main(argv=None):
    start = time.monotonic()
    out = Transcript(200)
    try:
        args = parse(sys.argv[1:] if argv is None else argv)
        out.bound = args.lines
        outcome, code = under_debugger(args, out, start + args.seconds)
    except (CannotRun, debugger.NotFound) as e:
        outcome, code = f"could not run: {e}", 2
    seconds = f"{time.monotonic() - start:.1f}"
    out.write(lambda lines: f"debug: {outcome}, {out.stops} stops, {lines} lines, {seconds} s", sys.stdout)
    return code


if __name__ == "__main__":
    sys.exit(main())
