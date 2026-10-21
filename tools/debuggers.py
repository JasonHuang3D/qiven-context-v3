"""The debuggers tools/debug.py and the watcher (tools/watch.py) drive:
each found and checked against its pin, and their output bounded.

- Windows: cdb.exe, the Windows SDK's, found from the registry's
  KitsRoot10 and Debuggers/x64/, its version (cdb -version) and SHA-256
  checked against tools/windows-pins.txt. It is run with -sins, so that no
  environment's symbol path or server is read, and -y the binary's
  directory.
- Linux: gdb on PATH, checked against its line of tools/linux-pins.txt
  ("command gdb VERSION SHA256 path/gdb"); none there or none on PATH, no
  debugger.
- hang_stacks(): every thread's stack of a running process, by the
  debugger attached without stopping it, and a dump of it; the watcher
  writes them before it kills a case past its limit.

Run by the pinned interpreter under -I, from its file (runpy): it imports
nothing of this directory.
"""

import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOWS = sys.platform == "win32"
WINDOWS_PINS = ROOT / "tools" / "windows-pins.txt"
LINUX_PINS = ROOT / "tools" / "linux-pins.txt"
LINE_POINTS = 300  # a line's code points at most, the cut's marker within them
CUT = "[cut]"
FRAMES = 32
HANG_THREADS = 16
HANG_LINES = 200  # the hang's lines at most, its first line among them
HANG_SECONDS = 25.0  # the debugger's time, within the 30 s after the limit
NO_GDB = "gdb not installed: needs the owner's word"
# A thread's header in cdb's ~*kn, and in gdb's thread apply all bt.
CDB_THREAD = re.compile(r"^[.#]?\s*\d+\s+Id: [0-9a-f]+\.[0-9a-f]+ ")
GDB_THREAD = re.compile(r"^Thread \d+ \(")
# cdb's and gdb's lines that say nothing of the case.
NOISE = re.compile("|".join((
    r"^ModLoad: ", r"^\([0-9a-f]+\.[0-9a-f]+\): .* chance", r"^Breakpoint \d+ hit$",
    r"^\*\*\* WARNING: Unable to verify checksum", r"^NatVis script ", r"^quit:$", r"^\[New LWP ",
    r"^\[Thread debugging using ", r"^Using host libthread_db ", r"^\[Inferior \d+ \(process \d+\) detached\]")))


class NotFound(Exception):
    """No debugger at hand, or one off its pin: the reason."""


def clip(line):
    """The line within LINE_POINTS code points, a longer one cut with [cut]."""
    line = line.rstrip("\r\n")
    return line if len(line) <= LINE_POINTS else line[:LINE_POINTS - len(CUT)] + CUT


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def pin(path, kind, name):
    """(version, digest, source) of the pin file's line KIND NAME, or None."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        fields = line.split("#", 1)[0].split()
        if fields[:2] == [kind, name] and len(fields) == 5:
            return fields[2], fields[3], fields[4]
    return None


def kits_root():
    """The Windows SDK's root, by the registry's KitsRoot10, or None."""
    import winreg
    for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots", 0,
                                winreg.KEY_READ | view) as key:
                return Path(winreg.QueryValueEx(key, "KitsRoot10")[0])
        except OSError:
            continue
    return None


def checked(path, version, digest, ask):
    """The tool's path once its SHA-256 and version hold, else NotFound."""
    got = sha256(path)
    if got != digest:
        raise NotFound(f"{path.name}'s SHA-256 is {got}; the pin {digest}")
    try:
        done = subprocess.run([str(path), *ask], capture_output=True, stdin=subprocess.DEVNULL, timeout=30,
                              check=False)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise NotFound(f"{path.name} {' '.join(ask)}: {e}") from e
    text = done.stdout.decode("utf-8", "replace")
    if not re.search(rf"(?<![\w.]){re.escape(version)}(?![\w.])", text):
        raise NotFound(f"{path.name} {' '.join(ask)} gives {text.strip()[:80]!r}; the pin is {version}")
    return path


def find_cdb():
    found = pin(WINDOWS_PINS, "debugger", "cdb")
    if found is None:
        raise NotFound(f"no line 'debugger cdb' in {WINDOWS_PINS.name}")
    version, digest, source = found
    root = kits_root()
    if root is None:
        raise NotFound("no Windows SDK: the registry names no KitsRoot10")
    path = root / source.removeprefix("kits/")
    if not path.is_file():
        raise NotFound(f"no cdb.exe in the SDK's {Path(source).parent}")
    return checked(path, version, digest, ["-version"])


def find_gdb():
    found = shutil.which("gdb")
    if found is None:
        raise NotFound(NO_GDB)
    pinned = pin(LINUX_PINS, "command", "gdb")
    if pinned is None:
        raise NotFound(f"gdb is not pinned in {LINUX_PINS.name}")
    version, digest, _ = pinned
    return checked(Path(found).resolve(), version, digest, ["--version"])


def find():
    """The system's debugger, checked, or NotFound."""
    return find_cdb() if WINDOWS else find_gdb()


def module(binary):
    """cdb's name of a binary's module: its stem, each character other
    than a letter, a digit or _ as _."""
    return re.sub(r"[^A-Za-z0-9_]", "_", Path(binary).stem)


def hang_command(debugger, pid, binary, dump):
    """The debugger's command line: every thread's stack, then the dump
    (none when dump is None), the process left running."""
    if WINDOWS:
        commands = f"~*kn {FRAMES}" + (f"; .dump /mt {dump}" if dump else "") + "; qd"
        return [str(debugger), "-pv", "-p", str(pid), "-sins", "-lines", "-y", str(Path(binary).parent),
                "-c", commands]
    steps = ["set pagination off", "set debuginfod enabled off", f"thread apply all bt {FRAMES}"]
    steps += [f"gcore {dump}"] if dump else []
    return [str(debugger), "-p", str(pid), "-batch", "-nx", "-q",
            *(a for s in [*steps, "detach"] for a in ("-ex", s))]


def hang_lines(text):
    """The stacks of the debugger's output: (threads, lines), the threads
    past HANG_THREADS dropped, the lines within HANG_LINES - 1."""
    lines = text.splitlines()
    if WINDOWS:
        start = next((i + 1 for i, line in enumerate(lines) if "Reading initial command" in line), len(lines))
        lines = lines[start:]
    header = CDB_THREAD if WINDOWS else GDB_THREAD
    threads, kept, dropped = 0, [], False
    for line in lines:
        if not line.strip() or NOISE.match(line):
            continue
        if header.match(line):
            threads += 1
        if threads > HANG_THREADS and not line.startswith(("Creating ", "Dump ", "Saved corefile ")):
            dropped = True
            continue
        kept.append(clip(line))
    room = HANG_LINES - 1
    if dropped or len(kept) > room:
        kept = kept[:room - 1] + [f"{CUT} {max(threads - HANG_THREADS, 0)} threads past {HANG_THREADS}, "
                                  f"or lines past {HANG_LINES}"]
    return threads, kept


def hang_stacks(pid, binary, dumps):
    """Every thread's stack of the running process pid, its binary's
    symbols beside it, and its dump <binary>-<pid>-hang.dmp (.core) in
    dumps when it is given: (threads, lines). NotFound when no debugger
    is at hand or it cannot run."""
    debugger = find()
    dump = None
    if dumps:
        dump = Path(dumps) / f"{Path(binary).stem}-{pid}-hang.{'dmp' if WINDOWS else 'core'}"
    try:
        done = subprocess.run(hang_command(debugger, pid, binary, dump), capture_output=True,
                              stdin=subprocess.DEVNULL, timeout=HANG_SECONDS, check=False)
    except subprocess.TimeoutExpired as e:
        return hang_lines((e.stdout or b"").decode("utf-8", "replace"))
    except OSError as e:
        raise NotFound(f"{Path(debugger).name}: {e}") from e
    return hang_lines(done.stdout.decode("utf-8", "replace") + done.stderr.decode("utf-8", "replace"))
