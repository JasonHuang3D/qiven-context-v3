"""The diagnostic entry's planted faults, K1 to K5, K7 and K9: each plant of
qiven3-fault, run as a process, ends with its cause as the report's first
line, its stack, its status, no window, within the bounds; and K18, the
Linux handlers' object calling only the async-signal-safe list.

The report, on standard error:

    fatal: <cause>
    fatal: stack <n>
      #<i> <frame>                      (n lines)
    fatal: events <n>
    fatal: dump <file name> | none
    fatal: end <status>

On Windows the frames are dbghelp's, symbolized in the process from the
binary's PDB: a build that keeps none beside the binary (Release, whose
binaries the clean-binaries check reads) names each frame's module and
offset alone, and the plant's name is read where the PDB is. On Linux the
frames are backtrace_symbols_fd's raw lines, a module, an offset and an
address each, which the watcher symbolizes after the exit: the plant's
frame is read here by its offset, inside plant_<plant>'s range in the
binary's symbol table. K3's and K4's library writes its own line before
the report (glibc's assertion, libstdc++'s hardened check), whose cause is
then the signal. K3 runs in qiven3-fault-crtdbg on Windows, on the debug
runtime. Under a sanitizer, whose report stands for the entry's, every
case is skipped. The raw report is read here; the watcher names statuses
and symbolizes Linux's frames later.
"""

import os
import re
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.session import build_option

FAULT = "qiven3-fault"
CRTDBG = "qiven3-fault-crtdbg"
TIME_BOUND = 10.0
LINES_BOUND = 60
LINE_POINTS = 300
FRAMES_MAX = 32
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


def windows_of(pid):
    """The visible top-level windows of process `pid`."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    found = []
    proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def each(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(proc(each), 0)
    return found


def run_plant(binary, plant, env):
    """Runs a plant: its status, out, err, seconds, and the windows seen."""
    start = time.monotonic()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        process = subprocess.Popen([str(binaries.path(binary)), plant], stdin=subprocess.DEVNULL,
                                   stdout=out, stderr=err, env=env)
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
        return status, out.read(), err.read(), seconds, seen


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
    if (len(rest) != 3 or rest[0] != "fatal: events 0" or not rest[1].startswith("fatal: dump ")
            or not rest[2].startswith("fatal: end ")):
        problems.append(f"{case}: the report's end: {rest}")
    return problems


# backtrace_symbols_fd's line after the report's "  #<i> ": the module, its
# symbol where the dynamic symbol table names one, the offset, the address.
FRAME = re.compile(r"  #\d+ (?P<module>[^()]*)\((?P<symbol>[^()+]*)\+0x(?P<offset>[0-9a-f]+)\) ?\[0x[0-9a-f]+\]")
# The async-signal-safe list signal.cpp's object may call (K18).
SAFE = frozenset(("write", "_exit", "raise", "sigaction", "getpid", "backtrace", "backtrace_symbols_fd"))


def nm(*args):
    """The build's nm, on args: its lines."""
    done = subprocess.run([build_option("CMAKE_NM") or "nm", *args], capture_output=True, check=True,
                          timeout=binaries.TIMEOUT)
    return done.stdout.decode("utf-8", "replace").splitlines()


def in_plant(binary, plant, frames):
    """Whether a frame of the binary's own, by its offset, falls in
    plant_<plant>'s range of the binary's symbol table."""
    ranges = []
    for line in nm("--defined-only", "-C", "-S", "-P", str(binaries.path(binary))):
        fields = line.split()
        if len(fields) == 4 and fields[0].startswith(f"plant_{plant}("):
            start = int(fields[2], 16)
            ranges.append((start, start + int(fields[3], 16)))
    for frame in frames:
        found = FRAME.fullmatch(frame)
        if found and Path(found["module"]).name == binary and not found["symbol"]:
            offset = int(found["offset"], 16)
            if any(start <= offset <= end for start, end in ranges):
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
        number, linux_cause, preface = LINUX[plant]
        with tempfile.TemporaryDirectory() as dumps:
            env = dict(os.environ, QIVEN3_DUMP_DIR=dumps)
            ran, out, err, seconds, seen = run_plant(binary, plant, env)
            text = err.decode("utf-8", "replace")
            lines = text.splitlines()
            self.assertEqual(ran, status if WINDOWS else -number, f"{case}: {text}")
            self.assertEqual(seen, [], f"{case}: a window opened")
            self.assertLessEqual(seconds, TIME_BOUND, case)
            self.assertLessEqual(len(lines) + len(out.splitlines()), LINES_BOUND, case)
            self.assertTrue(all(len(line) <= LINE_POINTS for line in lines), case)
            if not WINDOWS:
                cause = linux_cause
                if preface is not None:
                    self.assertRegex(lines[0] if lines else "", f"^{preface}$", f"{case}: {text}")
                    lines = lines[1:]
            self.assertEqual(report_problems(lines, cause, case), [])
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
                # backtrace_symbols_fd's raw lines, which the watcher
                # symbolizes after the exit; the plant's frame by its offset.
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

    def test_no_dump_dir(self):
        """With QIVEN3_DUMP_DIR unset the report says so: dump none."""
        if not WINDOWS:
            self.skipTest("Linux writes no minidump")
        env = {k: v for k, v in os.environ.items() if k != "QIVEN3_DUMP_DIR"}
        ran, _, err, _, _ = run_plant(FAULT, "failfast", env)
        self.assertEqual(ran, 0xC0000409)
        self.assertIn("fatal: dump none", err.decode("utf-8", "replace").splitlines())

    def test_report_problems_control(self):
        """The shape check's control: a report cut after its stack is caught."""
        cut = ["fatal: abort", "fatal: stack 1", "  #0 qiven3-fault!plant_abort+0x9"]
        self.assertNotEqual(report_problems(cut, "abort", "control"), [])
        self.assertNotEqual(report_problems(["abort"], "abort", "control"), [])


class Signal(unittest.TestCase):
    """K18: signal.cpp's object calls only the async-signal-safe list, by its
    undefined symbols; its control, a copy with a call of malloc planted."""
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
        source = binaries.ROOT / "src" / "diag" / "signal.cpp"
        with tempfile.TemporaryDirectory() as top:
            copy = Path(top) / "signal.cpp"
            copy.write_bytes(source.read_bytes() + b"\n#include <cstdlib>\nvoid* qiven3_planted() { return std::malloc(1); }\n")
            obj = Path(top) / "signal.o"
            subprocess.run([build_option("CMAKE_CXX_COMPILER"), "-std=c++20", "-O2", "-fno-exceptions", "-c", "-I",
                            str(binaries.ROOT / "src"), "-o", str(obj), str(copy)], check=True, capture_output=True,
                           timeout=binaries.TIMEOUT)
            self.assertIn("malloc", undefined(obj) - SAFE)


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
