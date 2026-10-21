"""The diagnostic entry's planted faults, K1 to K5, K7 and K9: each plant of
qiven3-fault, run as a process, ends with its cause as the report's first
line, its stack, its status, no window, within the bounds.

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
frames are backtrace_symbols_fd's raw lines, which the watcher symbolizes
after the exit, so the plant's name is not read here yet. K3 runs in
qiven3-fault-crtdbg, on the debug runtime. On Linux the entry's signal
handlers are not in this build yet: K7 and K9, the terminate handler's,
run, and the others are skipped. Under a sanitizer, whose report stands for
the entry's, every case is skipped. The raw report is read here; the
watcher names statuses and symbolizes Linux's frames later.
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

# plant: (case, binary, cause pattern, Windows status, Linux signal or None).
PLANTS = {
    "null": ("K1", FAULT, r"access violation reading 0x0000000000000000", 0xC0000005, None),
    "abort": ("K2", FAULT, r"abort", 3, None),
    "assert": ("K3", CRTDBG if WINDOWS else FAULT, r"assertion failed: false, \S*fault_assert\.cpp:\d+", 3, None),
    "parameter": ("K4", FAULT, r"invalid parameter(: .*)?", 0xC0000417, None),
    "overflow": ("K5", FAULT, r"stack overflow", 0xC00000FD, None),
    "exception": ("K7", FAULT, r"unhandled exception: plant", 3, signal.SIGABRT),
    "failfast": ("K9", FAULT, r"fail-fast: plant", 0xC0000409, signal.SIGABRT),
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


class Faults(unittest.TestCase):
    ctest_labels = ("fake",)

    def setUp(self):
        if sanitized():
            self.skipTest("under a sanitizer its report stands for the entry's")

    def plant(self, plant):
        case, binary, cause, status, linux_signal = PLANTS[plant]
        if not WINDOWS and linux_signal is None:
            self.skipTest(f"{case}: Linux's signal handlers are not in this build yet")
        with tempfile.TemporaryDirectory() as dumps:
            env = dict(os.environ, QIVEN3_DUMP_DIR=dumps)
            ran, out, err, seconds, seen = run_plant(binary, plant, env)
            text = err.decode("utf-8", "replace")
            lines = text.splitlines()
            self.assertEqual(ran, status if WINDOWS else -linux_signal, f"{case}: {text}")
            self.assertEqual(seen, [], f"{case}: a window opened")
            self.assertLessEqual(seconds, TIME_BOUND, case)
            self.assertLessEqual(len(lines) + len(out.splitlines()), LINES_BOUND, case)
            self.assertTrue(all(len(line) <= LINE_POINTS for line in lines), case)
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
                self.assertEqual(lines[-1], "fatal: end SIGABRT")
                # backtrace_symbols_fd's raw lines, an address each, which the
                # watcher symbolizes after the exit: the plant's name is read then.
                self.assertTrue(all(re.search(r"\[0x[0-9a-f]+\]$", f) for f in frames), f"{case}: {frames}")
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
