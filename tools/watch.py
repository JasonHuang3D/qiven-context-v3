"""The watcher: in front of every test CTest runs, and of each planted
fault the drives run, it runs the case and names how it ended.

    python -I -X faulthandler tools/watch.py --limit <s> --name <test> -- <command>...

- With QIVEN_BUILD naming the build, it sets QIVEN_DUMP_DIR to the
  build's dumps/ and QIVEN_LOG to the build's logs/<test>.jsonl, each
  directory made; and QIVEN_WATCHER to its own process id. Then it runs
  the command, which inherits its standard output, its standard input
  closed.
- It first sets its own error mode as the diagnostic entry's first stage
  does (system.py's quiet_load_errors); the case inherits it, so that a
  library missing at load opens no window.
- The case's standard error is passed through line by line. The frames'
  symbolizing on Linux and its loader's path are retired (tools/README.md's
  Retired section).
- Then one line on standard error names the status by
  foundation/devkit/python/status.py's table, "status: <value> <name>:
  <meaning>". On a library missing at
  load (system.py's missing_library_status) no code of the binary ran, so it names, by
  foundation/devkit/structure/imports.py's reader, each library the binary imports
  found nowhere the loader looks: "...; not found: <library>, ...".
- It exits with the case's status, a signal on Linux as 128 plus its
  number.
- Past its limit, the hang: by tools/debuggers.py's debugger, attached
  without stopping the case, it prints "hang: past its limit of <s> s,
  <n> threads" and every thread's stack, at most 16 threads and 200 lines,
  and writes <binary>-<pid>-hang.dmp in the dumps'
  directory; with no debugger at hand, "hang: no debugger, no stacks" and
  the reason. The debugger is the pinned one, but in a CI job on Windows,
  where the image keeps its own tools, the image's cdb, unpinned
  (tools/debuggers.py's hang_debugger). Then it kills the case's process
  tree (foundation/devkit/python/process.py's end_tree), names the status
  with what it wrote ("no
  debugger, no stack written: <why>", or the stacks written of the
  threads and its dump or "no dump", and by a CI image's cdb its version
  and path) and the case,
  "status: 124 hang: ...; <written>; <test> past <s> s", and exits 124.
- After the case ends, a hang's or not, the build's dumps/ is bounded by
  tools/dumps.py: at most its DUMPS_KEPT dumps, the oldest removed first,
  none the case wrote; a line before the status line names each removed.

Run by the pinned interpreter under -I, which keeps this directory off
sys.path: it appends the root once and imports the status table, the
system's module, the process's and the build tree's root; the debugger's, the dumps' and the
imports' readers are run from their files.
"""

import argparse
import os
import re
import runpy
import sys
import threading
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent))  # under -I the checkout is off sys.path; appended, so that the standard library still wins
from foundation.devkit.python import process
from foundation.devkit.python import status as status_table
from foundation.devkit.python import system
from foundation.devkit.python.build_tree import ROOT
DEBUGGERS = runpy.run_path(str(ROOT / "tools" / "debuggers.py"))
DUMPS = runpy.run_path(str(ROOT / "tools" / "dumps.py"))
IMPORTS = runpy.run_path(str(ROOT / "foundation" / "devkit" / "structure" / "imports.py"))
HANG = 124
READER_WAIT = 5.0  # seconds the stream's reader is waited for after the case ends


def safe_name(name):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name)


def environment(name):
    env = dict(os.environ, QIVEN_WATCHER=str(os.getpid()))
    build = os.environ.get("QIVEN_BUILD")
    if build:
        dumps, logs = Path(build) / "dumps", Path(build) / "logs"
        dumps.mkdir(exist_ok=True)
        logs.mkdir(exist_ok=True)
        env["QIVEN_DUMP_DIR"] = str(dumps)
        env["QIVEN_LOG"] = str(logs / (safe_name(name) + ".jsonl"))
    return env


def relay(stream, sink):
    """Passes the case's standard error through."""
    for raw in iter(stream.readline, b""):
        sink.write(raw)
        sink.flush()
    sink.flush()


def missing(command):
    """Each library the command's binary imports found nowhere the loader
    looks, by the reader; None when the binary cannot be read."""
    found = process.find(command[0])
    if found is None:
        return None
    binary = Path(found).resolve()
    try:
        names = IMPORTS["read_file"](binary).names
    except (OSError, IMPORTS["Unreadable"]):
        return None
    dirs = system.loader_dirs(binary)
    out = []
    for name in names:
        if name.lower().startswith(("api-ms-", "ext-ms-")):
            continue  # an API set, which the system resolves
        if not any((d / name).is_file() for d in dirs):
            out.append(name)
    return out


def status_line(status, command):
    text = status_table.line(status)
    if status == system.missing_library_status():
        names = missing(command)
        if names is None:
            text += "; its imports cannot be read"
        elif names:
            text += "; not found: " + ", ".join(names)
    return text


def exit_code(status):
    """The watcher's own exit code for the case's status."""
    if status < 0:
        return 128 - status
    if status >= 1 << 31:
        return status - (1 << 32)  # a Windows status; sys.exit takes a C int: the same 32 bits
    return status


def hang(case, command, env, limit):
    """The hang's lines, every thread's stack by the debugger, and what it
    wrote, for the status line: "no debugger, no stack written: <why>",
    or "<n> of <threads> threads' stacks written" ("no stack written" for
    none) and "its dump <file>" or "no dump", then, by a CI image's
    debugger, ", by the image's cdb <version> at <path>, unpinned (CI)"."""
    binary = process.find(command[0]) or command[0]
    dumps = env.get("QIVEN_DUMP_DIR")
    try:
        threads, lines, how = DEBUGGERS["hang_stacks"](case.pid, binary, dumps)
    except DEBUGGERS["NotFound"] as e:
        return ["hang: no debugger, no stacks", f"hang: {e}"], f"no debugger, no stack written: {e}"
    stacks = DEBUGGERS["stacks_of"](lines)
    dump = DEBUGGERS["hang_dump"](case.pid, binary, dumps)
    written = f"{stacks} of {threads} threads' stacks written" if stacks else "no stack written"
    written += f", its dump {dump.name}" if dump is not None and dump.is_file() else ", no dump"
    written += f", {how}" if how else ""
    return [f"hang: past its limit of {limit:g} s, {threads} threads", *lines], written


def bound(case):
    """Bounds the build's dumps/, keeping the case's own, and names each
    dump removed."""
    build = os.environ.get("QIVEN_BUILD")
    lines = DUMPS["bound"](Path(build) / "dumps", case.pid) if build else []
    if lines:
        print("\n".join(lines), file=sys.stderr, flush=True)


def parse(argv):
    parser = argparse.ArgumentParser(prog="watch.py", description="Runs a case and names its exit status.")
    parser.add_argument("--limit", type=float, required=True, help="seconds before the case is a hang")
    parser.add_argument("--name", required=True, help="the test's name, its log file's")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- and the case's command")
    args = parser.parse_args(argv)
    if args.command[:1] == ["--"]:
        args.command = args.command[1:]
    if not args.command:
        parser.error("no command after --")
    return args


def main(argv=None):
    args = parse(sys.argv[1:] if argv is None else argv)
    system.quiet_load_errors()
    try:
        env = environment(args.name)
        case = process.start(args.command, env=env, out=process.INHERIT)
    except OSError as e:
        print(f"watch.py: {args.name}: the case cannot start: {e}", file=sys.stderr, flush=True)
        return 2
    reader = threading.Thread(target=relay, args=(case.stderr, sys.stderr.buffer), daemon=True)
    reader.start()
    try:
        status = case.wait(args.limit)
    except process.TimedOut:
        lines, written = hang(case, args.command, env, args.limit)
        print("\n".join(lines), file=sys.stderr, flush=True)
        case.end_tree()
        reader.join(READER_WAIT)
        bound(case)
        print(f"{status_table.line(HANG)}; {written}; {args.name} past {args.limit:g} s", file=sys.stderr, flush=True)
        return HANG
    reader.join(READER_WAIT)
    bound(case)
    print(status_line(status, args.command), file=sys.stderr, flush=True)
    return exit_code(status)


if __name__ == "__main__":
    sys.exit(main())
