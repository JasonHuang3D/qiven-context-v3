"""gdb's half of tools/debug.py (Linux): a case, a binary or a core file
under gdb in batch mode, its output read into debug.py's transcript.

- gdb runs as -batch -nx -q, with pagination, confirmation, thread events
  and debuginfod off, so that nothing reaches the network, set before the
  binary loads (-iex); breakpoints are made pending when their library is
  not loaded yet, and the case starts with no shell.
- Batch mode reads no commands from its input: the whole session is its
  -ex commands, each run alone, so that one in error (a bad expression)
  is printed and the next still runs. The commands of every stop up to the
  stop bound are given at once, each led by an echoed marker; the output
  is cut at the markers, and once the run has ended, faulted or reached
  the stop bound, the rest is dropped: gdb runs those commands on (past a
  fault, the case's own handlers then run), and its exit ends the case.
- A stop is a breakpoint's, a watchpoint's or a signal's. A signal, at a
  stop or within a step, is the fault, gdb seeing it before the case's own
  handlers: the last stop, its actions taken, the outcome "fault <signal>,
  <its description>".
- The commands: break, watch, info locals, bt 32, thread apply all bt 32,
  print, step, next, run and continue; a core read as gdb <binary> <core>,
  the binary named by the core's notes (NT_PRPSINFO's name, NT_FILE's
  path) found in --symbols, by default the core's directory.
- Past the time bound gdb's process group, the case within it, is killed.

Run by tools/debug.py from its file (runpy): it imports nothing of this
directory, and is given what it shares with cdb's half.
"""

import os
import re
import signal
import struct
import subprocess
import time
from pathlib import Path

MARK = "@qiven3-debug@"
SETTINGS = ("set pagination off", "set confirm off", "set debuginfod enabled off", "set breakpoint pending on",
            "set print thread-events off", "set startup-with-shell off")
EXITED = re.compile(r"^\[Inferior \d+ \(process \d+\) exited (?:normally|with code (?P<code>[0-7]+))\]$")
TERMINATED = re.compile(r"^Program terminated with signal (?P<signal>SIG\w+), (?P<what>.*?)\.?$")
SIGNALLED = re.compile(r"^(?:Program|Thread \d+ .*) received signal (?P<signal>SIG\w+), (?P<what>.*?)\.?$")
STOPPED = re.compile(r"^(?:Thread \d+ .* hit )?(?:Temporary breakpoint|Breakpoint|Hardware watchpoint|Watchpoint|"
                     r"Hardware read watchpoint|Hardware access \(read/write\) watchpoint|Catchpoint) \d+")
NT_PRPSINFO, NT_FILE, PT_NOTE = 3, 0x46494C45, 4


def gdb_argv(gdb, steps, tail=()):
    """gdb's command line: the settings before the binary loads, then each
    (marker, command) as its own -ex, the marker echoed first."""
    argv = [str(gdb), "-batch", "-nx", "-q", *(a for s in SETTINGS for a in ("-iex", s))]
    for marker, command in steps:
        argv += ["-ex", f"echo \\n{MARK} {marker}\\n", "-ex", command]
    return argv + list(tail)


def run_steps(args, actions):
    """The session's (marker, command) steps: the breaks, the run, and at
    each stop its actions and the run's continuing, up to the stop bound."""
    steps = [(f"> break {spec}", f"break {spec}") for spec in args.break_]
    steps.append(("go", "run"))
    for stop in range(1, args.stops + 1):
        if stop == 1:
            steps += [(f"> watch {e}", f"watch {e}") for e in args.watch]
        steps += [(f"> {echo}", command) for echo, command in actions]
        if stop < args.stops:
            steps.append(("go", "continue"))
    return steps


def stop_actions(args, frames):
    """(echo, command) of each action at a stop, in the text's order."""
    out = [("locals", "info locals")] if args.locals else []
    out += [("stack", f"bt {frames}")] if args.stack else []
    out += [("threads", f"thread apply all bt {frames}")] if args.threads else []
    out += [(f"print {e}", f"print {e}") for e in args.print]
    out += [(f"step {args.step}", f"step {args.step}")] if args.step else []
    out += [(f"next {args.next}", f"next {args.next}")] if args.next else []
    return out


def segments(text, noise):
    """The output cut at the markers: [(marker, lines)], the lines before
    the first marker under None; blank lines and noise dropped."""
    out = [(None, [])]
    for line in text.replace("\r\n", "\n").split("\n"):
        if line.startswith(MARK + " "):
            out.append((line[len(MARK) + 1:], []))
        elif line.strip() and not noise.match(line):
            out[-1][1].append(line)
    return out


def signal_number(name):
    try:
        return signal.Signals[name].value
    except KeyError:
        return None


def exited(status, status_table):
    found = status_table["named"](status)
    return f"exited {status_table['value_text'](status)} {found[0] if found else status_table['UNNAMED']}"


def ending(lines, status_table):
    """(outcome, code) of a run's end in the lines, or None."""
    for line in lines:
        done = EXITED.match(line)
        if done:
            return exited(int(done["code"] or "0", 8), status_table), 0
        done = TERMINATED.match(line)
        if done:
            number = signal_number(done["signal"])
            return (exited(-number, status_table), 0) if number else (f"exited {done['signal']}", 0)
    return None


def read_run(text, out, stops, noise, status_table):
    """Reads a run's output into the transcript: (outcome, code), or None
    when it ended unread (gdb ended, or was killed, first)."""
    final = None  # the fault's, once its stop's actions are read
    for marker, lines in segments(text, noise)[1:]:
        if marker != "go":
            out.add(marker)
            for line in lines:
                out.add(line)
            end = None if final else ending(lines, status_table)
            if end:
                return end
            # A step's signal is the fault too: the stop's other actions are taken.
            fault = None if final else next((SIGNALLED.match(line) for line in lines if SIGNALLED.match(line)), None)
            if fault:
                final = f"fault {fault['signal']}, {fault['what']}", 1
            continue
        if final:
            return final
        end = ending(lines, status_table)
        if end:
            for line in lines:
                if not (EXITED.match(line) or TERMINATED.match(line)):
                    out.add(line)
            return end
        at = next((i for i, line in enumerate(lines) if SIGNALLED.match(line) or STOPPED.match(line)), None)
        if at is None:
            for line in lines:
                out.add(line)
            return None
        out.stops += 1
        out.add(f"stop {out.stops}: {lines[at]}")
        for line in lines[:at] + lines[at + 1:]:
            out.add(line)
        fault = SIGNALLED.match(lines[at])
        if fault:
            final = f"fault {fault['signal']}, {fault['what']}", 1
    if final:
        return final
    return ("stop bound", 0) if out.stops >= stops else None


def notes(path):
    """The core file's notes: [(type, descriptor)]."""
    found = []
    with open(path, "rb") as f:
        head = f.read(64)
        phoff, = struct.unpack_from("<Q", head, 0x20)
        size, count = struct.unpack_from("<HH", head, 0x36)
        for i in range(count):
            f.seek(phoff + i * size)
            kind, _, offset, _, _, filesz = struct.unpack("<IIQQQQ", f.read(40))
            if kind != PT_NOTE:
                continue
            f.seek(offset)
            data, at = f.read(filesz), 0
            while at + 12 <= len(data):
                namesz, descsz, note = struct.unpack_from("<III", data, at)
                at += 12 + (namesz + 3) // 4 * 4
                found.append((note, data[at:at + descsz]))
                at += (descsz + 3) // 4 * 4
    return found


def core_binary(path):
    """The binary's file name the core's notes give, or None."""
    try:
        found = notes(path)
    except (OSError, struct.error):
        return None
    name = next((d[40:56].split(b"\0", 1)[0].decode("utf-8", "replace") for t, d in found
                 if t == NT_PRPSINFO and len(d) >= 56), None)
    for kind, desc in found:
        if kind == NT_FILE and len(desc) >= 16:
            entries, = struct.unpack_from("<Q", desc, 0)
            paths = desc[16 + 24 * entries:].split(b"\0")
            for each in (Path(p.decode("utf-8", "replace")).name for p in paths if p):
                if name and each.startswith(name):
                    return each
    return name or None


def session(gdb, argv, deadline, env=None, cwd=None):
    """Runs gdb to its end or the deadline: (its output, timed out)."""
    try:
        process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   env=env, cwd=cwd, process_group=0)
    except OSError as e:
        return f"{Path(gdb).name}: {e}", None
    try:
        data, _ = process.communicate(timeout=max(deadline - time.monotonic(), 0.1))
        return data.decode("utf-8", "replace"), False
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except OSError:
            pass
        process.kill()
        data, _ = process.communicate()
        return data.decode("utf-8", "replace"), True


def run(gdb, args, command, env, cwd, out, deadline, shared):
    """case and run: (outcome, code)."""
    steps = run_steps(args, stop_actions(args, shared["FRAMES"]))
    text, timed_out = session(gdb, gdb_argv(gdb, steps, ["--args", *command]), deadline, env, cwd)
    if timed_out is None:
        return f"could not run: {text}", 2
    got = read_run(text, out, args.stops, shared["NOISE"], shared["STATUS"])
    if timed_out:
        return "timed out", 1
    return got or (f"could not run: {Path(command[0]).name}: the debugger ended", 2)


def dump(gdb, core, symbols, out, deadline, shared):
    """dump: the core's context, its stack and locals, every thread's
    stack: (outcome, code)."""
    name = core_binary(core)
    if name is None:
        return f"could not run: {core}'s notes name no binary", 2
    binary = Path(symbols) / name
    if not binary.is_file():
        return f"could not run: no binary {name} in {symbols}", 2
    frames = shared["FRAMES"]
    steps = [("> context", "frame"), ("> stack", f"bt {frames}"), ("> locals", "info locals"),
             ("> threads", f"thread apply all bt {frames}")]
    text, timed_out = session(gdb, gdb_argv(gdb, steps, [str(binary), str(core)]), deadline)
    if timed_out is None:
        return f"could not run: {text}", 2
    parts = segments(text, shared["NOISE"])
    fault = next((TERMINATED.match(line) for line in parts[0][1] if TERMINATED.match(line)), None)
    out.add(f"dump: {fault.group(0) if fault else 'no event'}")
    for marker, lines in parts[1:]:
        out.add(marker)
        for line in lines:
            out.add(line)
    if timed_out:
        return "timed out", 1
    return (f"fault {fault['signal']}, {fault['what']}", 1) if fault else ("dump read", 0)
