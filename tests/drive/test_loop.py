"""The loop's command, tools/loop.py: its exit codes, 0 when every step
passed, 1 when one failed and 2 when it could not run, each with its plant
(a failing step: a planted compile error, on Windows, or a planted
configure failure, in WSL, each on a scratch copy of the tree; a tool
missing; a pin off its digest; a bad argument); its bound of 100 lines and
a failing step's last 30. The cases that build run alone; those that reach
WSL skip when it or its distribution is absent."""

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

from tests.drive.test_loop_ci import ROOT, load_loop

LOOP = load_loop()
WINDOWS = sys.platform == "win32"
PLANT = "planted by the loop's case"
TIMEOUT = 1800
COPY_IGNORED = shutil.ignore_patterns("build", ".codegraph", "__pycache__")


def run(script, *args, env=None):
    done = subprocess.run([sys.executable, "-I", str(script), *args], stdin=subprocess.DEVNULL, capture_output=True,
                          env=env, timeout=TIMEOUT)
    return done.returncode, done.stdout.decode("utf-8", "replace").splitlines(), done.stderr


def wsl_ready():
    wsl = shutil.which("wsl.exe") if WINDOWS else None
    try:
        return bool(wsl) and LOOP.DISTRO in LOOP.distros(wsl)
    except LOOP.CannotRun:
        return False


def turned(digest):
    return ("1" if digest[0] == "0" else "0") + digest[1:]


def scratch_copy(case):
    """A copy of the tree, its git directory included, removed after the case."""
    top = Path(tempfile.mkdtemp(prefix="qiven3-loop-case-"))
    case.addCleanup(shutil.rmtree, top, ignore_errors=True)
    copy = top / "tree"
    shutil.copytree(ROOT, copy, ignore=COPY_IGNORED)
    return copy


class Answer(unittest.TestCase):
    """The answer's form, by steps of the interpreter alone."""

    def steps(self, codes, phase="test"):
        steps = [LOOP.Step(f"step {i}", ("step", str(i)), "build" if i < 2 else "test", after=None)
                 for i in range(len(codes))]
        logs = Path(tempfile.mkdtemp(prefix="qiven3-loop-logs-"))
        self.addCleanup(shutil.rmtree, logs, ignore_errors=True)
        report = LOOP.Report(LOOP.LINES)
        report.plan(len(LOOP.chosen(steps, phase)))
        out = io.StringIO()

        def command(step):
            code = codes[int(step.ci[1])]
            return [sys.executable, "-I", "-c", f"[print('line', i) for i in range(200)]; raise SystemExit({code})"]

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
        self.assertEqual(lines[2], "  its last 30 of 201 lines:")
        tail = [line for line in lines if line.startswith("  | ")]
        self.assertEqual(tail, [f"  | line {i}" for i in range(170, 200)])

    def test_bound(self):
        """Forty failing steps: every step's line and the summary within 100
        lines, the first tails whole and none past the bound."""
        counts, lines = self.steps([5] * 40)
        self.assertEqual(counts, [0, 40, 0])
        self.assertLessEqual(len(lines), LOOP.LINES)
        self.assertEqual(sum(1 for line in lines if re.match(r"job step \d+: FAIL", line)), 40)
        self.assertEqual(lines[-1], "summary")
        self.assertEqual(lines[1], "  its last 30 of 201 lines:")

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


class Arguments(unittest.TestCase):
    def test_refused(self):
        script = ROOT / "tools" / "loop.py"
        for args in (["frob", "windows"], ["build", "plan9"], ["test", "windows", "no-such-job"],
                     ["build", "windows", "linux-gcc"], ["build", "linux", "ci-windows"], []):
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


def prefix():
    """The toolchain's prefix in WSL, from QIVEN3_PREFIX: a case needing it
    skips without it."""
    if not os.environ.get("QIVEN3_PREFIX"):
        raise unittest.SkipTest("QIVEN3_PREFIX is unset: the toolchain's prefix in WSL")
    return os.environ["QIVEN3_PREFIX"]


@unittest.skipUnless(WINDOWS, "the loop's windows runs on Windows")
class Windows(unittest.TestCase):
    ctest_serial = True

    def test_tool_missing(self):
        """vcvars' PATH without the tools: exit 2, cmake named."""
        top = Path(tempfile.mkdtemp(prefix="qiven3-loop-case-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        vcvars = top / "vcvars64.bat"
        vcvars.write_text('@set "PATH=%SystemRoot%\\system32"\r\n@set VCToolsVersion=14.44.35207\r\n'
                          '@set WindowsSDKVersion=10.0.26100.0\\\r\n', encoding="ascii")
        env = dict(os.environ, QIVEN3_VCVARS=str(vcvars))
        # The loop's files alone in the scratch, with the status table it
        # reads, so that its logs are the scratch's.
        (top / "tools").mkdir()
        for name in ("loop.py", "loop_jobs.py", "status.py"):
            shutil.copy2(ROOT / "tools" / name, top / "tools" / name)
        status, lines, _ = run(top / "tools" / "loop.py", "build", "windows", "windows-msvc", env=env)
        self.assertEqual(status, 2, lines)
        self.assertIn("cmake: missing from vcvars' PATH", lines[-1])
        self.assertTrue(lines[-1].endswith("; exit 2"))

    def test_pin_off(self):
        """ninja's pin off its digest: exit 2, the digests named."""
        pins = LOOP.WINDOWS_PINS
        self.addCleanup(setattr, LOOP, "WINDOWS_PINS", pins)
        LOOP.WINDOWS_PINS = tuple((n, v, turned(d) if n == "ninja" else d) for n, v, d in pins)

        def refuse(*_):  # the real tree is never built or emptied, even if the plant were missed
            raise AssertionError("the loop went past its pins")

        for name in ("own_tree", "run_steps"):
            self.addCleanup(setattr, LOOP, name, getattr(LOOP, name))
            setattr(LOOP, name, refuse)
        # The loop's logs, vcvars' script among them, in the case's scratch.
        top = Path(tempfile.mkdtemp(prefix="qiven3-loop-case-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        self.addCleanup(setattr, LOOP, "ROOT", LOOP.ROOT)
        LOOP.ROOT = top
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = LOOP.main(["build", "windows", "windows-msvc"])
        lines = out.getvalue().splitlines()
        self.assertEqual(status, 2, lines)
        digest = dict((n, d) for n, _, d in pins)["ninja"]
        # The pin alone: no other check failing beside it.
        self.assertRegex(lines[-1], f"^loop: cannot run: ninja: [^;]+'s SHA-256 is {digest}; "
                                    f"the pin {turned(digest)}; exit 2$")
        self.assertNotIn("configure", out.getvalue())

    def test_upper_names(self):
        """vcvars under names upper-cased, as a copy of os.environ holds
        them (the watcher's): a name inherited keeps its case through
        vcvars, VCTOOLSVERSION here, and the loop reads it by name without
        regard to case, so the tools hold."""
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
        copy = scratch_copy(self)
        source = copy / "src" / "memory" / "arena.cpp"
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
        self.assertRegex(lines[-1], r"^loop: 1 passed, 1 failed, 0 skipped in \d+\.\d s; exit 1$")


class Linux(unittest.TestCase):
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
        self.assertRegex(lines[-1], r"^loop: 0 passed, 1 failed, 2 skipped in .*; exit 1;")


if __name__ == "__main__":
    unittest.main()
