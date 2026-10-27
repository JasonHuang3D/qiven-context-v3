"""The watcher: in front of every test CTest runs, and of each planted
fault the drives run, it runs the case and names how it ended.

    python -I -X faulthandler tools/watch.py --limit <s> --name <test> -- <command>...

- With QIVEN3_BUILD naming the build, it sets QIVEN3_DUMP_DIR to the
  build's dumps/ and QIVEN3_LOG to the build's logs/<test>.jsonl, each
  directory made; and QIVEN3_WATCHER to its own process id. Then it runs
  the command, which inherits its standard input and output.
- On Windows it first sets its own error mode as the diagnostic entry's
  first stage does; the case inherits it, so that a library missing at
  load opens no window.
- The case's standard error is passed through line by line. On Linux
  each run of the report's frames, backtrace_symbols_fd's raw lines, is
  symbolized after it ends: a frame in a module with no symbol named gets
  addr2line's function and line appended where the binary has symbols, read
  at the byte before the frame's address, its call's, the outermost
  function where calls were inlined
  (QIVEN3_ADDR2LINE, or addr2line on PATH; none, the frames stay raw).
- Then one line on standard error names the status by tools/status.py's
  table, "status: <value> <name>: <meaning>". On a library missing at
  load (0xC0000135, or 127) no code of the binary ran, so it names, by
  tests/structure/imports.py's reader, each library the binary imports
  found nowhere the loader looks: "...; not found: <library>, ...".
- It exits with the case's status, a signal on Linux as 128 plus its
  number.
- Past its limit, the hang: by tools/debuggers.py's debugger, attached
  without stopping the case, it prints "hang: past its limit of <s> s,
  <n> threads" and every thread's stack, at most 16 threads and 200 lines,
  and writes <binary>-<pid>-hang.dmp (.core on Linux) in the dumps'
  directory; with no debugger at hand, "hang: no debugger, no stacks" and
  the reason. The debugger is the pinned one, but in a CI job on Windows,
  where the image keeps its own tools, the image's cdb, unpinned
  (tools/debuggers.py's hang_debugger). Then it kills the case's process
  tree (taskkill /T /F; on Linux the case's process group, which it
  started, by SIGKILL), names the status with what it wrote ("no
  debugger, no stack written: <why>", or the stacks written of the
  threads and its dump or "no dump", and by a CI image's cdb its version
  and path) and the case,
  "status: 124 hang: ...; <written>; <test> past <s> s", and exits 124.
- After the case ends, a hang's or not, the build's dumps/ is bounded by
  tools/dumps.py: at most its DUMPS_KEPT dumps, the oldest removed first,
  none the case wrote; a line before the status line names each removed.

Run by the pinned interpreter under -I, which keeps this directory off
sys.path: the table and the reader are run from their files.
"""

import argparse
import os
import re
import runpy
import shutil
import signal
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATUS = runpy.run_path(str(ROOT / "tools" / "status.py"))
DEBUGGERS = runpy.run_path(str(ROOT / "tools" / "debuggers.py"))
DUMPS = runpy.run_path(str(ROOT / "tools" / "dumps.py"))
IMPORTS = runpy.run_path(str(ROOT / "tests" / "structure" / "imports.py"))
WINDOWS = sys.platform == "win32"
# SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, as the entry sets.
ERROR_MODE = 0x0001 | 0x0002 | 0x8000
DLL_NOT_FOUND, LOADER = 0xC0000135, 127
HANG = 124
READER_WAIT = 5.0  # seconds the stream's reader is waited for after the case ends
# backtrace_symbols_fd's line after the report's "  #<i> ": the module, its
# symbol where the dynamic symbol table names one, the offset, the address.
FRAME = re.compile(rb"  #\d+ (?P<module>[^()]*)\((?P<symbol>[^()+]*)\+0x(?P<offset>[0-9a-f]+)\) ?\[0x[0-9a-f]+\]")
LINUX_DIRS = ("/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", "/lib64", "/usr/lib64", "/lib", "/usr/lib")


def safe_name(name):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name)


def environment(name):
    env = dict(os.environ, QIVEN3_WATCHER=str(os.getpid()))
    build = os.environ.get("QIVEN3_BUILD")
    if build:
        dumps, logs = Path(build) / "dumps", Path(build) / "logs"
        dumps.mkdir(exist_ok=True)
        logs.mkdir(exist_ok=True)
        env["QIVEN3_DUMP_DIR"] = str(dumps)
        env["QIVEN3_LOG"] = str(logs / (safe_name(name) + ".jsonl"))
    return env


def addr2line():
    return os.environ.get("QIVEN3_ADDR2LINE") or shutil.which("addr2line")


def symbolized(frames):
    """The frames, each in a module with no symbol named followed by its
    function and line where addr2line finds them."""
    tool = addr2line()
    found = [FRAME.fullmatch(f.rstrip(b"\r\n")) for f in frames]
    wanted = {}
    for m in found:
        if m and not m["symbol"] and m["module"] and Path(m["module"].decode()).is_file():
            wanted.setdefault(m["module"].decode(), set()).add(m["offset"].decode())
    names = {}
    for module, offsets in wanted.items() if tool else ():
        offsets = sorted(offsets)
        try:
            # Each frame but the fault's is a return address, past its call,
            # which may end its function: the byte before it is the call's.
            # -a heads each address's lines; -i gives its inlined chain.
            done = subprocess.run([tool, "-a", "-f", "-C", "-i", "-e", module,
                                   *(f"{int(o, 16) - 1:#x}" for o in offsets)],
                                  capture_output=True, timeout=30, check=False)
        except (OSError, subprocess.TimeoutExpired):
            continue
        groups = []
        for text in done.stdout.decode("utf-8", "replace").splitlines():
            if re.fullmatch(r"0x[0-9a-f]+", text):
                groups.append([])
            elif groups:
                groups[-1].append(text)
        for offset, group in zip(offsets, groups if done.returncode == 0 else ()):
            # The outermost of an inlined chain is the frame's own function.
            function, where = (group[-2:] if len(group) >= 2 else ["??", "??"])
            if function != "??":
                names[(module, offset)] = function if where.startswith("??") else f"{function} {where}"
    out = []
    for frame, m in zip(frames, found):
        name = names.get((m["module"].decode(), m["offset"].decode())) if m else None
        out.append(frame if name is None else frame.rstrip(b"\r\n") + b" " + name.encode() + b"\n")
    return out


def relay(stream, sink):
    """Passes the case's standard error through; on Linux each run of
    frames is symbolized once it ends."""
    held = []
    for raw in iter(stream.readline, b""):
        if not WINDOWS and FRAME.fullmatch(raw.rstrip(b"\r\n")):
            held.append(raw)
            continue
        if held:
            sink.writelines(symbolized(held))
            held = []
        sink.write(raw)
        sink.flush()
    if held:
        sink.writelines(symbolized(held))
    sink.flush()


def search_path(binary):
    """The directories the loader looks in for the binary's libraries."""
    if WINDOWS:
        system = os.environ.get("SystemRoot")
        dirs = [binary.parent, *((Path(system) / "System32", Path(system)) if system else ())]
        return dirs + [Path(p) for p in os.environ.get("PATH", "").split(os.pathsep) if p]
    dirs = [Path(p) for p in os.environ.get("LD_LIBRARY_PATH", "").split(":") if p]
    for conf in [Path("/etc/ld.so.conf"), *sorted(Path("/etc/ld.so.conf.d").glob("*.conf"))]:
        try:
            text = conf.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        dirs += [Path(line.strip()) for line in text.splitlines() if line.strip().startswith("/")]
    return dirs + [Path(d) for d in LINUX_DIRS]


def missing(command):
    """Each library the command's binary imports found nowhere the loader
    looks, by the reader; None when the binary cannot be read."""
    found = shutil.which(command[0])
    if found is None:
        return None
    binary = Path(found).resolve()
    try:
        names = IMPORTS["read_file"](binary).names
    except (OSError, IMPORTS["Unreadable"]):
        return None
    dirs = search_path(binary)
    out = []
    for name in names:
        if WINDOWS and name.lower().startswith(("api-ms-", "ext-ms-")):
            continue  # an API set, which the system resolves
        if not any((d / name).is_file() for d in dirs):
            out.append(name)
    return out


def status_line(status, command):
    text = STATUS["line"](status)
    if status == (DLL_NOT_FOUND if WINDOWS else LOADER):
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
    if WINDOWS and status >= 1 << 31:
        return status - (1 << 32)  # sys.exit takes a C int: the same 32 bits
    return status


def kill_tree(process):
    """Kills the case and every process it started."""
    try:
        if WINDOWS:
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], capture_output=True,
                           stdin=subprocess.DEVNULL, timeout=30, check=False)
        else:
            os.killpg(process.pid, signal.SIGKILL)
    except (OSError, subprocess.TimeoutExpired):
        pass
    process.kill()


def hang(process, command, env, limit):
    """The hang's lines, every thread's stack by the debugger, and what it
    wrote, for the status line: "no debugger, no stack written: <why>",
    or "<n> of <threads> threads' stacks written" ("no stack written" for
    none) and "its dump <file>" or "no dump", then, by a CI image's
    debugger, ", by the image's cdb <version> at <path>, unpinned (CI)"."""
    binary = shutil.which(command[0]) or command[0]
    dumps = env.get("QIVEN3_DUMP_DIR")
    try:
        threads, lines, how = DEBUGGERS["hang_stacks"](process.pid, binary, dumps)
    except DEBUGGERS["NotFound"] as e:
        return ["hang: no debugger, no stacks", f"hang: {e}"], f"no debugger, no stack written: {e}"
    stacks = DEBUGGERS["stacks_of"](lines)
    dump = DEBUGGERS["hang_dump"](process.pid, binary, dumps)
    written = f"{stacks} of {threads} threads' stacks written" if stacks else "no stack written"
    written += f", its dump {dump.name}" if dump is not None and dump.is_file() else ", no dump"
    written += f", {how}" if how else ""
    return [f"hang: past its limit of {limit:g} s, {threads} threads", *lines], written


def bound(process):
    """Bounds the build's dumps/, keeping the case's own, and names each
    dump removed."""
    build = os.environ.get("QIVEN3_BUILD")
    lines = DUMPS["bound"](Path(build) / "dumps", process.pid) if build else []
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
    if WINDOWS:
        import ctypes
        ctypes.WinDLL("kernel32").SetErrorMode(ERROR_MODE)
    try:
        env = environment(args.name)
        # On Linux the case leads its own process group, which a hang kills.
        process = subprocess.Popen(args.command, stderr=subprocess.PIPE, env=env,
                                   **({} if WINDOWS else {"process_group": 0}))
    except OSError as e:
        print(f"watch.py: {args.name}: the case cannot start: {e}", file=sys.stderr, flush=True)
        return 2
    reader = threading.Thread(target=relay, args=(process.stderr, sys.stderr.buffer), daemon=True)
    reader.start()
    try:
        status = process.wait(timeout=args.limit)
    except subprocess.TimeoutExpired:
        lines, written = hang(process, args.command, env, args.limit)
        print("\n".join(lines), file=sys.stderr, flush=True)
        kill_tree(process)
        process.wait()
        reader.join(READER_WAIT)
        bound(process)
        print(f"{STATUS['line'](HANG)}; {written}; {args.name} past {args.limit:g} s", file=sys.stderr, flush=True)
        return HANG
    reader.join(READER_WAIT)
    bound(process)
    print(status_line(status, args.command), file=sys.stderr, flush=True)
    return exit_code(status)


if __name__ == "__main__":
    sys.exit(main())
