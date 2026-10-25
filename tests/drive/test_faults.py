"""The diagnostic entry's planted faults, K1 to K5, K7 and K9: each plant of
qiven3-fault, run behind the watcher (tools/watch.py), ends with its cause
as the report's first line, its stack, its status named by the table
(K12), no window, within the bounds; K6, qiven3-fault-import copied alone,
its library missing at load; and K18, the Linux handlers' object calling
only the async-signal-safe list.

The report, on standard error:

    fatal: <cause>
    fatal: stack <n>
      #<i> <frame>                      (n lines)
    fatal: events <n>
    <event>                           (n lines, JSON)
    fatal: dump <file name> | none
    fatal: end <status>
    status: <value> <name>: <meaning>    (the watcher's)

On Windows the frames are dbghelp's, symbolized in the process from the
binary's PDB: a build that keeps none beside the binary (Release, whose
binaries the clean-binaries check reads) names each frame's module and
offset alone, and the plant's name is read where the PDB is. On Linux the
frames are backtrace_symbols_fd's raw lines, a module, an offset and an
address each, which the watcher symbolizes: a frame of the binary's own
gets its function and line appended, plant_<plant> among them. K3's and
K4's library writes its own line before the report (glibc's assertion,
libstdc++'s hardened check), whose cause is then the signal. K3 runs in
qiven3-fault-crtdbg on Windows, on the debug runtime; in a Debug build,
whose qiven3-fault is on that runtime too, K4's fclose reports the
runtime's own assertion before its invalid parameter, status 3. Each
run keeps a Log's ring and ends 20 operations in it first: the events
are the last 16 lines, op.end of operation 20 the last. Under a sanitizer,
whose report stands for the entry's, every case is skipped. Dumps are in
the build's dumps/, where the watcher sends them.
"""

import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.session import build_option
from tools import status as status_table

FAULT = "qiven3-fault"
CRTDBG = "qiven3-fault-crtdbg"
IMPORT = "qiven3-fault-import"
LIBRARY = "qiven3_fault_lib.dll" if sys.platform == "win32" else "libqiven3_fault_lib.so"
TIME_BOUND = 10.0
LINES_BOUND = 60
LINE_POINTS = 300
FRAMES_MAX = 32
EVENTS_MAX = 16
OPERATIONS = 20
DUMP_MAX = 64 * 1024 * 1024
POLL = 0.1
WINDOWS = sys.platform == "win32"

# plant: (case, binary, Windows cause pattern, Windows status).
PLANTS = {
    "null": ("K1", FAULT, r"access violation reading 0x0000000000000000", 0xC0000005),
    "abort": ("K2", FAULT, r"abort", 3),
    "assert": ("K3", CRTDBG if WINDOWS else FAULT, r"assertion failed: false, \S*fault_assert\.cpp:\d+", 3),
    "parameter": ("K4", FAULT, r"invalid parameter(: .*)?", 0xC0000417),
    "overflow": ("K5", FAULT, r"stack overflow", 0xC00000FD),
    "exception": ("K7", FAULT, r"unhandled exception: plant", 3),
    "failfast": ("K9", FAULT, r"fail-fast: plant", 0xC0000409),
}
# plant: (Linux signal, cause pattern, the library's line before the report or None).
LINUX = {
    "null": (signal.SIGSEGV, r"SIGSEGV at 0x0000000000000000", None),
    "abort": (signal.SIGABRT, r"SIGABRT", None),
    "assert": (signal.SIGABRT, r"SIGABRT", r".*fault_assert\.cpp:\d+: .*Assertion [`']false' failed\."),
    "parameter": (signal.SIGABRT, r"SIGABRT", r".*stl_vector\.h:\d+: .*Assertion '.*' failed\."),
    "overflow": (signal.SIGSEGV, r"SIGSEGV at 0x[0-9A-F]{16}, stack overflow", None),
    "exception": (signal.SIGABRT, r"unhandled exception: plant", None),
    "failfast": (signal.SIGABRT, r"fail-fast: plant", None),
}


def sanitized():
    return bool(build_option("QIVEN3_SANITIZE"))


def debug_runtime():
    """Whether the build's binaries are on the debug runtime: a Debug build."""
    return WINDOWS and build_option("CMAKE_BUILD_TYPE") == "Debug"


def windows_of(pid):
    """The visible top-level windows of process `pid` and of its children,
    the watcher's case among them."""
    import ctypes
    from ctypes import wintypes

    class Entry(ctypes.Structure):  # PROCESSENTRY32W
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
                    ("heap", ctypes.c_size_t), ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", ctypes.c_long), ("flags", wintypes.DWORD),
                    ("file", wintypes.WCHAR * 260)]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    snapshot = kernel32.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
    pids, entry = {pid}, Entry()
    entry.size = ctypes.sizeof(Entry)
    more = kernel32.Process32FirstW(wintypes.HANDLE(snapshot), ctypes.byref(entry))
    while more:
        if entry.parent == pid:
            pids.add(entry.pid)
        more = kernel32.Process32NextW(wintypes.HANDLE(snapshot), ctypes.byref(entry))
    kernel32.CloseHandle(wintypes.HANDLE(snapshot))
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    found = []
    proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def each(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value in pids and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(proc(each), 0)
    return found


def run_plant(binary, plant, env=None):
    """Runs a plant behind the watcher: its status, out, err, seconds, and
    the windows seen."""
    return run_watched([binaries.path(binary), plant], PLANTS[plant][0], env)


def run_watched(command, name, env=None, cwd=None):
    """Runs a command behind the watcher, under the time bound: the
    watcher's status, out, err, seconds, and the windows seen."""
    start = time.monotonic()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        process = subprocess.Popen(binaries.watched(command, name, TIME_BOUND), stdin=subprocess.DEVNULL,
                                   stdout=out, stderr=err, env=env, cwd=cwd)
        seen = []
        while process.poll() is None:
            if WINDOWS:
                seen += windows_of(process.pid)
            if time.monotonic() - start > TIME_BOUND + 20:
                process.kill()
                break
            time.sleep(POLL)
        status = process.wait()
        seconds = time.monotonic() - start
        out.seek(0)
        err.seek(0)
        return status, out.read(), binaries.unbounded(err.read()), seconds, seen


def report_problems(lines, cause, case):
    """What the report's shape lacks, as a list of strings."""
    problems = []
    if not lines or not re.fullmatch("fatal: " + cause, lines[0]):
        problems.append(f"{case}: the first line is not the cause {cause!r}: {lines[:1]}")
        return problems
    head = re.fullmatch(r"fatal: stack (\d+)", lines[1]) if len(lines) > 1 else None
    if head is None:
        return problems + [f"{case}: no stack line"]
    n = int(head.group(1))
    frames = lines[2:2 + n]
    if not 0 < n <= FRAMES_MAX or any(not f.startswith(f"  #{i} ") for i, f in enumerate(frames)):
        problems.append(f"{case}: {n} frames, or a frame out of order")
    rest = lines[2 + n:]
    head = re.fullmatch(r"fatal: events (\d+)", rest[0]) if rest else None
    events = int(head.group(1)) if head else -1
    if not 0 <= events <= EVENTS_MAX:
        return problems + [f"{case}: no events line, or past {EVENTS_MAX}: {rest[:1]}"]
    for line in rest[1:1 + events]:
        try:
            if not isinstance(json.loads(line), dict):
                problems.append(f"{case}: an event not an object: {line}")
        except ValueError:
            problems.append(f"{case}: an event not JSON: {line}")
    rest = rest[1 + events:]
    if (len(rest) != 2 or not rest[0].startswith("fatal: dump ") or not rest[1].startswith("fatal: end ")):
        problems.append(f"{case}: the report's end: {rest}")
    return problems


def events_of(lines):
    """A report's events, each a dict, by its events line."""
    at = next(i for i, line in enumerate(lines) if line.startswith("fatal: events "))
    return [json.loads(line) for line in lines[at + 1:at + 1 + int(lines[at].split()[2])]]


# backtrace_symbols_fd's line after the report's "  #<i> ": the module, its
# symbol where the dynamic symbol table names one, the offset, the address;
# then the watcher's function and line, where it found them.
FRAME = re.compile(r"  #\d+ (?P<module>[^()]*)\((?P<symbol>[^()+]*)\+0x(?P<offset>[0-9a-f]+)\) ?\[0x[0-9a-f]+\]"
                   r"(?: (?P<function>.+))?")
# The async-signal-safe list signal.cpp's object may call (K18).
SAFE = frozenset(("write", "_exit", "raise", "sigaction", "getpid", "backtrace", "backtrace_symbols_fd"))


def nm(*args):
    """The build's nm, on args: its lines."""
    done = subprocess.run([build_option("CMAKE_NM") or "nm", *args], capture_output=True, check=True,
                          timeout=binaries.TIMEOUT)
    return done.stdout.decode("utf-8", "replace").splitlines()


def in_plant(binary, plant, frames):
    """Whether a frame of the binary's own is plant_<plant>, by the
    watcher's symbolized name."""
    for frame in frames:
        found = FRAME.fullmatch(frame)
        if found and Path(found["module"]).name == binary and (found["function"] or "").startswith(
                f"plant_{plant}("):
            return True
    return False


def undefined(obj):
    """An object's undefined symbols, by nm."""
    return {line.split()[0] for line in nm("-u", "-P", str(obj)) if line.strip()}


class Faults(unittest.TestCase):
    ctest_labels = ("fake",)

    def setUp(self):
        if sanitized():
            self.skipTest("under a sanitizer its report stands for the entry's")

    def plant(self, plant):
        case, binary, cause, status = PLANTS[plant]
        if plant == "parameter" and debug_runtime():
            # The debug runtime's own assertion in fclose reaches its hook first.
            cause, status = r"assertion failed: stream\.valid\(\), \S*fclose\.cpp:\d+", 3
        number, linux_cause, preface = LINUX[plant]
        dumps = binaries.directory("QIVEN3_BUILD") / "dumps"
        ran, out, err, seconds, seen = run_plant(binary, plant)
        text = err.decode("utf-8", "replace")
        lines = text.splitlines()
        self.assertEqual(ran, status if WINDOWS else 128 + number, f"{case}: {text}")
        self.assertEqual(seen, [], f"{case}: a window opened")
        self.assertLessEqual(seconds, TIME_BOUND, case)
        self.assertLessEqual(len(lines) + len(out.splitlines()), LINES_BOUND, case)
        self.assertTrue(all(len(line) <= LINE_POINTS for line in lines), case)
        # K12: the watcher's last line names the status by the table.
        named = status_table.line(status if WINDOWS else -number)
        self.assertEqual(lines[-1] if lines else "", named, f"{case}: {text}")
        self.assertNotIn(status_table.UNNAMED, named, case)
        lines = lines[:-1]
        if not WINDOWS:
            cause = linux_cause
            if preface is not None:
                self.assertRegex(lines[0] if lines else "", f"^{preface}$", f"{case}: {text}")
                lines = lines[1:]
        self.assertEqual(report_problems(lines, cause, case), [])
        events = events_of(lines)
        self.assertEqual(len(events), EVENTS_MAX, f"{case}: {events}")
        ops = [e.get("op") for e in events]
        self.assertEqual(ops, sorted(ops), f"{case}: not oldest first: {events}")
        self.assertEqual({k: events[-1].get(k) for k in ("event", "name", "op", "outcome")},
                         {"event": "op.end", "name": "plant", "op": OPERATIONS, "outcome": "ok"}, case)
        n = int(lines[1].split()[2])
        frames = lines[2:2 + n]
        dump = lines[-2].removeprefix("fatal: dump ")
        if WINDOWS:
            self.assertEqual(lines[-1], f"fatal: end {status}" if status == 3 else f"fatal: end 0x{status:08X}")
            if (binaries.path(binary).with_suffix(".pdb")).is_file():
                self.assertTrue(any(f"!plant_{plant}+" in f for f in frames), f"{case}: {frames}")
            else:
                self.assertTrue(all(f" {binary}" in f or "!" in f for f in frames), f"{case}: {frames}")
            self.assertRegex(dump, rf"^{re.escape(binary)}-\d+\.dmp$", case)
            written = Path(dumps) / dump
            self.assertLessEqual(written.stat().st_size, DUMP_MAX, case)
            self.assertEqual(written.read_bytes()[:4], b"MDMP", case)
        else:
            self.assertEqual(lines[-1], f"fatal: end {signal.Signals(number).name}")
            # backtrace_symbols_fd's raw lines, the watcher's names after
            # each of the binary's own; the plant's frame by its name.
            self.assertTrue(all(FRAME.fullmatch(f) for f in frames), f"{case}: {frames}")
            self.assertTrue(in_plant(binary, plant, frames), f"{case}: no frame in plant_{plant}: {frames}")
            # The reading's control: plant_out, on no fault's stack, is found in none.
            self.assertFalse(in_plant(binary, "out", frames), f"{case}: {frames}")
            self.assertEqual(dump, "none")

    def test_K1_null(self):
        self.plant("null")

    def test_K2_abort(self):
        self.plant("abort")

    def test_K3_assert(self):
        self.plant("assert")

    def test_K4_parameter(self):
        self.plant("parameter")

    def test_K5_overflow(self):
        self.plant("overflow")

    def test_K7_exception(self):
        self.plant("exception")

    def test_K9_failfast(self):
        self.plant("failfast")

    def test_K6_import(self):
        """qiven3-fault-import copied alone: its library missing at load, the
        watcher's line names it; beside its library it exits 0."""
        expected = 0xC0000135 if WINDOWS else 127
        with tempfile.TemporaryDirectory() as top:
            copy = Path(top) / binaries.path(IMPORT).name
            shutil.copy2(binaries.path(IMPORT), copy)
            ran, out, err, seconds, seen = run_watched([copy], "K6", cwd=top)
        text = err.decode("utf-8", "replace")
        lines = text.splitlines()
        self.assertEqual(ran, expected, text)
        self.assertEqual(seen, [], "K6: a window opened")
        self.assertLessEqual(seconds, TIME_BOUND)
        self.assertLessEqual(len(lines) + len(out.splitlines()), LINES_BOUND)
        self.assertEqual(lines[-1] if lines else "", f"{status_table.line(expected)}; not found: {LIBRARY}")
        if not WINDOWS:  # the loader's own line, before the watcher's
            self.assertTrue(any(LIBRARY in line and "cannot open shared object file" in line
                                for line in lines[:-1]), text)
        # The control: beside its library the binary loads and exits 0.
        env = dict(os.environ)
        if not WINDOWS:
            env["LD_LIBRARY_PATH"] = str(binaries.directory("QIVEN3_BIN"))
        ran, _, err, _, _ = run_watched([binaries.path(IMPORT)], "K6.control", env)
        self.assertEqual((ran, err.decode("utf-8", "replace").splitlines()[-1:]), (0, ["status: 0 passed"]))

    def test_K12_unnamed(self):
        """K12's control: a status off the table, printed unnamed, the
        watcher exiting with it."""
        ran, _, err, _, _ = run_watched([sys.executable, "-I", "-c", "import sys; sys.exit(77)"], "K12")
        self.assertEqual((ran, err.decode("utf-8", "replace").splitlines()), (77, ["status: 77 unnamed"]))
        self.assertEqual(status_table.line(0xC0000001), "status: 0xC0000001 unnamed")
        self.assertEqual(status_table.line(-31), "status: signal 31 unnamed")

    def test_no_dump_dir(self):
        """With QIVEN3_DUMP_DIR unset the report says so: dump none. The
        plant runs alone, the watcher setting the variable."""
        if not WINDOWS:
            self.skipTest("Linux writes no minidump")
        env = {k: v for k, v in os.environ.items() if k != "QIVEN3_DUMP_DIR"}
        ran = subprocess.run([str(binaries.path(FAULT)), "failfast"], capture_output=True, env=env,
                             timeout=binaries.TIMEOUT, check=False)
        self.assertEqual(ran.returncode, 0xC0000409)
        self.assertIn("fatal: dump none", ran.stderr.decode("utf-8", "replace").splitlines())

    def test_report_problems_control(self):
        """The shape check's control: a report cut after its stack is caught."""
        cut = ["fatal: abort", "fatal: stack 1", "  #0 qiven3-fault!plant_abort+0x9"]
        self.assertNotEqual(report_problems(cut, "abort", "control"), [])
        self.assertNotEqual(report_problems(["abort"], "abort", "control"), [])
        # Its events: one line fewer than the count, or one not JSON, is caught.
        whole = cut + ["fatal: events 1", '{"event":"op.end"}', "fatal: dump none", "fatal: end SIGABRT"]
        self.assertEqual(report_problems(whole, "abort", "control"), [])
        short = cut + ["fatal: events 2", '{"event":"op.end"}', "fatal: dump none", "fatal: end SIGABRT"]
        self.assertNotEqual(report_problems(short, "abort", "control"), [])
        broken = cut + ["fatal: events 1", '{"event":', "fatal: dump none", "fatal: end SIGABRT"]
        self.assertNotEqual(report_problems(broken, "abort", "control"), [])
        past = cut + ["fatal: events 17"] + ['{}'] * 17 + ["fatal: dump none", "fatal: end SIGABRT"]
        self.assertNotEqual(report_problems(past, "abort", "control"), [])


class Signal(unittest.TestCase):
    """K18: signal.cpp's object calls only the async-signal-safe list, by its
    undefined symbols; its control, a copy with a call of malloc planted."""
    ctest_reads = ("src/",)
    ctest_labels = ("fake",)

    def setUp(self):
        if WINDOWS:
            self.skipTest("Linux's handlers")
        if sanitized():
            self.skipTest("a sanitizer's instrumentation calls its own runtime")

    def test_K18_safe(self):
        obj = binaries.directory("QIVEN3_BUILD") / "src" / "diag" / "CMakeFiles" / "qiven3_diag.dir" / "signal.cpp.o"
        self.assertTrue(obj.is_file(), obj)
        self.assertEqual(undefined(obj) - SAFE, set())

    def test_K18_control(self):
        """The copy compiled by the build's own command for signal.cpp, its
        flags all kept (the hardening's among them), its source and object
        the copy's and no dependency file written into the build."""
        source = binaries.ROOT / "src" / "diag" / "signal.cpp"
        with tempfile.TemporaryDirectory() as top:
            copy = Path(top) / "signal.cpp"
            copy.write_bytes(source.read_bytes() + b"\n#include <cstdlib>\nvoid* qiven3_planted();\n"
                             b"void* qiven3_planted() { return std::malloc(1); }\n")
            obj = Path(top) / "signal.o"
            argv, directory = control_command(source, copy, obj)
            done = subprocess.run(argv, cwd=directory, capture_output=True, timeout=binaries.TIMEOUT, check=False)
            self.assertEqual(done.returncode, 0, done.stderr.decode("utf-8", "replace")[-2000:])
            self.assertIn("malloc", undefined(obj) - SAFE)


def control_command(source, copy, obj):
    """The build's compile command for source, from its
    compile_commands.json, compiling copy into obj instead: (argv, its
    directory). The dependency file's options are dropped."""
    entries = json.loads((binaries.directory("QIVEN3_BUILD") / "compile_commands.json").read_text(encoding="utf-8"))
    entry = next(e for e in entries if Path(e["file"]).resolve() == source.resolve())
    given = entry.get("arguments") or shlex.split(entry["command"])
    argv, skip = [], False
    for arg in given:
        if skip:
            skip = False
        elif arg in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif arg not in ("-MD", "-MMD"):
            argv.append(str(copy) if arg == entry["file"] else arg)
    if argv.count(str(copy)) != 1:
        raise AssertionError(f"the source is not once in its compile command: {given}")
    return argv + ["-o", str(obj)], entry["directory"]


class Out(unittest.TestCase):
    """diag::Out, through the plant out: standard error for QIVEN3_LOG=-, the
    file it names, appended to, and nothing when it is unset."""
    ctest_labels = ("fake",)

    def run_out(self, value):
        env = {k: v for k, v in os.environ.items() if k != "QIVEN3_LOG"}
        if value is not None:
            env["QIVEN3_LOG"] = value
        done = subprocess.run([str(binaries.path(FAULT)), "out"], capture_output=True, env=env,
                              timeout=binaries.TIMEOUT, check=False)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout, b"")
        return done.stderr

    def test_out(self):
        two = b"out: plant\n" * 2
        self.assertEqual(self.run_out(None), b"")
        self.assertEqual(self.run_out("-").replace(b"\r\n", b"\n"), two)
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / "case.jsonl"
            self.assertEqual(self.run_out(str(log)), b"")
            self.assertEqual(self.run_out(str(log)), b"")
            self.assertEqual(log.read_bytes(), two * 2)


if __name__ == "__main__":
    unittest.main()
