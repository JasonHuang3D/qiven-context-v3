"""The loop's command, tools/loop.py: its exit codes, 0 when every step
passed, 1 when one failed and 2 when it could not run, each with its plant
(a failing step: a planted compile error, on Windows, or a planted
configure failure, in WSL, each on a scratch copy of the tree; a tool
missing; a pin off its digest; a bad argument); its bound of 100 lines,
a failing step's first error lines and its last 30. The tiers on fakes and
Linux's one clone are tests/drive/test_loop_tiers.py's. The cases that
build run alone; those that reach WSL skip when it or its distribution is
absent, and those on the loop's pinned interpreter or Windows tools where
these do not hold, as on CI's images (tests/drive/pinned.py)."""

import contextlib
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive import pinned
from tests.drive.pinned import prefix, wsl_ready
from tests.drive.test_loop_ci import ROOT, load_loop

LOOP = load_loop()
WINDOWS = sys.platform == "win32"
PLANT = "planted by the loop's case"
TIMEOUT = 1800
# The root's build/ alone, not the build module's directory, and each .codegraph and __pycache__.
COPY_IGNORED = lambda d, _: {".codegraph", "__pycache__", *(["build"] if Path(d).resolve() == ROOT.resolve() else [])}
LOOP_FILES = sorted(p.name for p in (ROOT / "tools").glob("loop*.py")) + ["status.py", "trees.json"]


def loop_scratch(case):
    """A scratch directory holding the loop's files alone, with the status
    table they read, so that its logs and trees are the scratch's."""
    top = Path(tempfile.mkdtemp(prefix="qiven3-loop-case-"))
    case.addCleanup(shutil.rmtree, top, ignore_errors=True)
    (top / "tools").mkdir()
    for name in LOOP_FILES:
        shutil.copy2(ROOT / "tools" / name, top / "tools" / name)
    return top


def run(script, *args, env=None):
    done = subprocess.run([sys.executable, "-I", str(script), *args], stdin=subprocess.DEVNULL, capture_output=True,
                          env=env, timeout=TIMEOUT)
    return done.returncode, done.stdout.decode("utf-8", "replace").splitlines(), done.stderr


def turned(digest):
    return ("1" if digest[0] == "0" else "0") + digest[1:]


def scratch_copy(case):
    """A copy of the tree, its git directory included, removed after the case."""
    top = Path(tempfile.mkdtemp(prefix="qiven3-loop-case-"))
    case.addCleanup(shutil.rmtree, top, ignore_errors=True)
    copy = top / "tree"
    shutil.copytree(ROOT, copy, ignore=COPY_IGNORED)
    return copy


class Loop(unittest.TestCase):
    ctest_labels = ("fake",)
    ctest_git = True  # starts git: listed on the loop's build alone (foundation/devkit/test/runner/list_tests.py)


class Answer(Loop):
    """The answer's form, by steps of the interpreter alone."""

    def steps(self, codes, phase="test", program="[print('line', i) for i in range(200)]"):
        steps = [LOOP.Step(f"step {i}", ("step", str(i)), "build" if i < 2 else "test", after=None)
                 for i in range(len(codes))]
        logs = Path(tempfile.mkdtemp(prefix="qiven3-loop-logs-"))
        self.addCleanup(shutil.rmtree, logs, ignore_errors=True)
        report = LOOP.Report(LOOP.LINES)
        report.plan(len(LOOP.chosen(steps, phase)))
        out = io.StringIO()

        def command(step):
            code = codes[int(step.ci[1])]
            return [sys.executable, "-I", "-c", f"{program}; raise SystemExit({code})"]

        with contextlib.redirect_stdout(out):
            counts = LOOP.run_steps(report, "job", steps, phase, command, logs, logs, dict(os.environ))
            report.last("summary")
        return counts, out.getvalue().splitlines()

    def test_pass(self):
        counts, lines = self.steps([0, 0, 0])
        self.assertEqual(counts, [3, 0, 0])
        self.assertEqual(len(lines), 4)
        self.assertTrue(all(re.fullmatch(r"job step \d: pass in \d+\.\d s", line) for line in lines[:3]), lines)

    def test_build_phase(self):
        counts, lines = self.steps([0, 0, 1], phase="build")
        self.assertEqual(counts, [2, 0, 0])

    def test_tail(self):
        counts, lines = self.steps([0, 3])
        self.assertEqual(counts, [1, 1, 0])
        self.assertRegex(lines[1], r"^job step 1: FAIL, status: 3 abort: Windows: the C runtime.s abort, in \d+\.\d s; log .*02-step-1\.log$")
        self.assertEqual(lines[2], "job step 1: its last 30 of 201 lines:")
        tail = [line for line in lines if line.startswith("  | ")]
        self.assertEqual(tail, [f"  | line {i}" for i in range(170, 200)])

    def test_bound(self):
        """Forty failing steps, an error line first in each: every step's
        line and the summary within 100 lines, then each step's error
        line by turns as far as the room holds, no tail, the steps left
        with no line counted on the last."""
        counts, lines = self.steps([5] * 40, program="print('a.cpp:1:1: error: x'); [print(i) for i in range(200)]")
        self.assertEqual(counts, [0, 40, 0])
        self.assertLessEqual(len(lines), LOOP.LINES)
        self.assertTrue(all(re.match(r"job step \d+: FAIL", line) for line in lines[:40]), lines[:40])
        self.assertEqual(lines[40:42], ["job step 0: its first 1 of 1 error lines:",
                                        "  ! 2: a.cpp:1:1: error: x"])
        self.assertEqual(sum(1 for line in lines if line == "  ! 2: a.cpp:1:1: error: x"), 29)
        self.assertFalse(any(" | " in line for line in lines), lines)
        self.assertEqual(lines[-1], "summary (11 failing steps' lines left out by the bound)")

    def test_first_errors(self):
        """A compile's first errors far before its tail, as a libc++
        build's: each with its line in the log, before the last 30 lines."""
        program = ("print('FAILED: a.o'); print('a.cpp:458:10: error: no member named jthread'); "
                   "[print('note', i) for i in range(100)]; print('b.cpp:362:5: error: too long'); "
                   "[print('line', i) for i in range(200)]; print('ninja: build stopped: subcommand failed.')")
        counts, lines = self.steps([2], program=program)
        self.assertEqual(lines[1:5], ["job step 0: its first 3 of 3 error lines, before its tail:",
                                      "  ! 2: FAILED: a.o", "  ! 3: a.cpp:458:10: error: no member named jthread",
                                      "  ! 104: b.cpp:362:5: error: too long"])
        self.assertEqual(lines[5], "job step 0: its last 30 of 305 lines:")
        self.assertEqual(len(lines), 37)

    def test_error_lines(self):
        """An error line of each tool; and lines that are not."""
        found = ["a.cpp(3): error C2065: 'x': undeclared identifier", "a.obj : error LNK2019: unresolved external",
                 "LINK : fatal error LNK1120: 1 unresolved externals", "a.cpp(4): warning C4100: unused",
                 "a.cpp:3:1: error: unused variable 'x' [-Werror,-Wunused-variable]", "collect2: error: ld returned 1",
                 "CMake Error at CMakeLists.txt:3 (message):", "FAILED: src/a.o",
                 "  5/40 Test  #5: unit.store ...***Failed  0.2 sec", "1/2 Test #1: a ...***Exception: SegFault",
                 "The following tests FAILED:", "Traceback (most recent call last):", "AssertionError: 1 != 2",
                 "FAIL: test_tail (tests.drive.test_loop.Answer.test_tail)", "ERROR: test_x (a.B.test_x)",
                 "==7==WARNING: MemorySanitizer: use-of-uninitialized-value"]
        passed = ["cl /WX -Werror a.cpp", "100% tests passed, 0 tests failed out of 4", "a.cpp:3:1: warning: unused",
                  "  1/40 Test  #1: unit.a ......   Passed    0.1 sec", "-- Configuring done", "error count 0"]
        self.assertEqual([t for t in found if not LOOP.ERROR_LINE.search(t)], [])
        self.assertEqual([t for t in passed if LOOP.ERROR_LINE.search(t)], [])

    def test_skip(self):
        steps = [LOOP.Step("configure", ("x",), "build", after=None), LOOP.Step("build", ("x",), "build",
                                                                                after="configure")]
        logs = Path(tempfile.mkdtemp(prefix="qiven3-loop-logs-"))
        self.addCleanup(shutil.rmtree, logs, ignore_errors=True)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            counts = LOOP.run_steps(LOOP.Report(LOOP.LINES), "job", steps, "build",
                                    lambda step: [sys.executable, "-I", "-c", "raise SystemExit(1)"], logs, logs, None)
        self.assertEqual(counts, [0, 1, 1])
        self.assertIn("job build: skipped, configure did not pass", out.getvalue())


class Arguments(Loop):
    def test_refused(self):
        script = ROOT / "tools" / "loop.py"
        for args in (["frob", "windows"], ["build", "plan9"], ["test", "windows", "no-such-job"],
                     ["build", "windows", "linux-gcc"], ["build", "linux", "ci-windows"], [],
                     ["1", "windows", "windows-msvc"], ["4", "windows"], ["2", "windows", "--fuzz-time", "5"],
                     ["3", "windows", "--fuzz-time", "0"], ["test", "windows", "--fuzz-time", "5"]):
            with self.subTest(args=args):
                status, lines, err = run(script, *args)
                self.assertEqual(status, 2)
                self.assertEqual(len(lines), 1, lines)
                self.assertTrue(lines[0].startswith("loop: "), lines)
                self.assertEqual(err, b"")

    def test_prefix_missing(self):
        """linux with neither --prefix nor QIVEN3_PREFIX: exit 2, the line
        naming both; the toolchain's place is never the loop's own."""
        env = {k: v for k, v in os.environ.items() if k != "QIVEN3_PREFIX"}
        status, lines, err = run(ROOT / "tools" / "loop.py", "build", "linux", "linux-gcc", env=env)
        self.assertEqual(status, 2, lines)
        self.assertEqual(len(lines), 1, lines)
        self.assertIn("--prefix", lines[0])
        self.assertIn("QIVEN3_PREFIX", lines[0])
        self.assertEqual(err, b"")


@unittest.skipUnless(WINDOWS, "the loop's windows runs on Windows")
class Windows(Loop):
    ctest_serial = True

    def test_tool_missing(self):
        """vcvars' PATH without the tools: exit 2, cmake named."""
        pinned.python()
        top = loop_scratch(self)
        vcvars = top / "vcvars64.bat"
        vcvars.write_text('@set "PATH=%SystemRoot%\\system32"\r\n@set VCToolsVersion=14.44.35207\r\n'
                          '@set WindowsSDKVersion=10.0.26100.0\\\r\n', encoding="ascii")
        env = dict(os.environ, QIVEN3_VCVARS=str(vcvars))
        status, lines, _ = run(top / "tools" / "loop.py", "build", "windows", "windows-msvc", env=env)
        self.assertEqual(status, 2, lines)
        self.assertIn("cmake: missing from vcvars' PATH", lines[-1])
        self.assertTrue(lines[-1].endswith("; exit 2"))

    def test_pin_off(self):
        """ninja's pin off its digest, in a scratch copy of the loop's
        files: exit 2, the digests named. The scratch holds no source, so
        the real tree is never built or emptied even if the plant were
        missed; the loop's logs, vcvars' script among them, are its own."""
        pinned.python()
        pinned.windows_tools()
        top = loop_scratch(self)
        source = top / "tools" / "loop_windows.py"
        digest = dict((n, d) for n, _, d in LOOP.WINDOWS_PINS)["ninja"]
        text = source.read_text(encoding="utf-8")
        self.assertEqual(text.count(digest), 1)
        source.write_text(text.replace(digest, turned(digest)), encoding="utf-8")
        status, lines, _ = run(top / "tools" / "loop.py", "build", "windows", "windows-msvc")
        self.assertEqual(status, 2, lines)
        # The pin alone: no other check failing beside it.
        self.assertRegex(lines[-1], f"^loop: cannot run: ninja: [^;]+'s SHA-256 is {digest}; "
                                    f"the pin {turned(digest)}; exit 2$")
        self.assertNotIn("configure", "\n".join(lines))
        self.assertFalse((top / "build" / "windows-msvc").exists())

    def test_upper_names(self):
        """vcvars under names upper-cased, as a copy of os.environ holds
        them (the watcher's): a name inherited keeps its case through
        vcvars, VCTOOLSVERSION here, and the loop reads it by name without
        regard to case, so the tools hold."""
        pinned.windows_tools()
        top = Path(tempfile.mkdtemp(prefix="qiven3-loop-case-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        env = dict(os.environ, VCTOOLSVERSION=LOOP.VCVARS_VER)
        code = ("import runpy, sys; from pathlib import Path; loop = runpy.run_path(sys.argv[1]); "
                "env = loop['vcvars_env'](Path(sys.argv[2])); "
                "print([k for k in env if k.upper() == 'VCTOOLSVERSION']); "
                "loop['check_windows_tools'](env); print('holds')")
        done = subprocess.run([sys.executable, "-I", "-c", code, str(ROOT / "tools" / "loop.py"), str(top)],
                              stdin=subprocess.DEVNULL, capture_output=True, env=env, timeout=TIMEOUT)
        lines = done.stdout.decode("utf-8", "replace").splitlines()
        err = done.stderr.decode("utf-8", "replace")
        self.assertEqual(lines[:1], ["['VCTOOLSVERSION']"], (lines, err))  # the plant held
        self.assertEqual((done.returncode, lines[1:]), (0, ["holds"]), err[-600:])

    def test_failing_step(self):
        """A compile error planted in a scratch copy: exit 1, the build's
        tail holding it, within the bound."""
        pinned.python()
        pinned.windows_tools()
        copy = scratch_copy(self)
        source = copy / "foundation" / "core" / "memory" / "src" / "arena.cpp"
        source.write_bytes(source.read_bytes() + f"#error {PLANT}\n".encode())
        status, lines, err = run(copy / "tools" / "loop.py", "build", "windows", "windows-msvc")
        self.assertEqual(status, 1, lines)
        self.assertLessEqual(len(lines), LOOP.LINES)
        self.assertTrue(any(re.match(r"windows-msvc configure: pass", line) for line in lines), lines)
        failed = [line for line in lines if line.startswith("windows-msvc build: FAIL")]
        self.assertEqual(len(failed), 1, lines)
        log = Path(failed[0].rsplit("; log ", 1)[1])
        self.assertTrue(log.is_file() and log.is_relative_to(copy / "build" / "loop"), log)
        tail = [line for line in lines if line.startswith("  | ")]
        self.assertTrue(0 < len(tail) <= LOOP.TAIL, lines)
        self.assertTrue(any(PLANT in line for line in tail), lines)
        self.assertRegex(lines[-1], r"^loop: 1 passed, 1 failed, 0 skipped in \d+\.\d s; git 0; exit 1$")


class Linux(Loop):
    ctest_serial = True

    @classmethod
    def setUpClass(cls):
        if not wsl_ready():
            raise unittest.SkipTest(f"WSL with {LOOP.DISTRO} is absent")

    def test_pin_off(self):
        """cmake's pin off its digest in a scratch pin file: exit 2 before
        any clone or build, the mismatch named."""
        top = Path(tempfile.mkdtemp(prefix="qiven3-loop-case-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        text = (ROOT / "tools" / "linux-pins.txt").read_text(encoding="utf-8")
        line = re.search(r"^command cmake \S+ (\S+) ", text, re.MULTILINE)
        (top / "pins.txt").write_text(text.replace(line.group(0), line.group(0).replace(
            line.group(1), turned(line.group(1)))), encoding="utf-8")
        status, lines, _ = run(ROOT / "tools" / "loop.py", "build", "linux", "linux-gcc", "--pins",
                               str(top / "pins.txt"), "--prefix", prefix())
        self.assertEqual(status, 2, lines)
        self.assertTrue(any(re.search(r"mismatch: command cmake: .*SHA-256 is " + line.group(1), x) for x in lines),
                        lines)
        self.assertFalse(any("sync" in x or "snapshot" in x for x in lines), lines)
        self.assertTrue(lines[-1].startswith("loop: cannot run: the pin check exited 2"), lines)

    def test_tool_missing(self):
        status, lines, _ = run(ROOT / "tools" / "loop.py", "build", "linux", "linux-gcc", "--prefix",
                               "/nonexistent/qiven3-prefix")
        self.assertEqual(status, 2, lines)
        self.assertIn("the interpreter is missing from the prefix", lines[-1])

    def test_failing_step(self):
        """A configure failure planted in a scratch copy, uncommitted: it
        reaches the clone, exit 1, its tail holding it."""
        copy = scratch_copy(self)
        lists = copy / "CMakeLists.txt"
        lists.write_bytes(lists.read_bytes() + f'message(FATAL_ERROR "{PLANT}")\n'.encode())
        clone = f"/tmp/qiven3-loop-case-{os.getpid()}"
        wsl = shutil.which("wsl.exe")
        self.addCleanup(subprocess.run, [wsl, "-d", LOOP.DISTRO, "-e", "rm", "-rf", clone],
                        stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
        status, lines, _ = run(copy / "tools" / "loop.py", "test", "linux", "linux-gcc", "--clone", clone,
                               "--prefix", prefix())
        self.assertEqual(status, 1, lines)
        self.assertLessEqual(len(lines), LOOP.LINES)
        self.assertTrue(any(x.startswith("linux-gcc configure: FAIL") and f"; log {clone}/" in x for x in lines),
                        lines)
        self.assertTrue(any(x.startswith("  | ") and PLANT in x for x in lines), lines)
        self.assertEqual(sum(1 for x in lines if re.match(r"linux-gcc \S+: skipped", x)), 2, lines)
        self.assertRegex(lines[-1], r"^loop: 0 passed, 1 failed, 2 skipped in .*; git 0; exit 1;")

