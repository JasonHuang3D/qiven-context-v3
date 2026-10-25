"""The loop's answer, tools/loop_run.py's and tools/loop_linux.py's, by
steps of the interpreter alone. Its seconds: an inner run in its own
process, its monotonic clock running fast as WSL2's slewed guest clock
has, relayed as linux's from Windows: each step's seconds and the inner
summary's within the seconds in all, and the inner run bound to the lines
this side left, none dropped; loop_fuzz's seconds by the loop's clock,
the monotonic one running fast. Its failing steps: three failing builds of a
gate's form, fuzz's last, each one's first error lines shown within the
bound, the tails sharing what is left; an error line within its tail shown
there alone; a failing step with no error line keeping its tail."""

import contextlib
import io
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from tests.drive.test_loop_ci import ROOT, load_loop

LOOP = load_loop()
FAST = 2.0  # the slewed clock's pace against the host's: past the 1.3 to 4.6 percent seen, so that it shows in a second
SLEEP = 1.0  # each step's seconds

# The inner run: the clocks slewed first, then the loop's names, two
# sleeping steps, a flood of lines, and main's summary line.
INNER = """import pathlib, runpy, sys, time
loop_py, fast = sys.argv[1], float(sys.argv[2])
host = time.monotonic
origin = host()
time.monotonic = lambda: origin + (host() - origin) * fast
if hasattr(time, "clock_gettime"):  # every clock but the raw one slewed; perf_counter is CLOCK_MONOTONIC's
    get, raw, first = time.clock_gettime, time.CLOCK_MONOTONIC_RAW, {}
    def slewed(clock):
        t = get(clock)
        return t if clock == raw else first.setdefault(clock, t) + (t - first.setdefault(clock, t)) * fast
    time.clock_gettime = slewed
    time.perf_counter = lambda: slewed(time.CLOCK_MONOTONIC)
loop = runpy.run_path(loop_py)
report = loop["Report"](int(sys.argv[sys.argv.index("--lines") + 1]))
start = loop["RUN"]["now"]()
steps = [loop["Step"](f"sleep {i}", ("sleep",), "build", after=None) for i in range(2)]
argv = [sys.executable, "-I", "-c", "import time; time.sleep(%s)"]
counts = loop["run_steps"](report, "job", steps, "build", lambda step: argv, pathlib.Path(sys.argv[3]),
                             pathlib.Path(sys.argv[3]), None)
for i in range(150):
    report.line(f"inner line {i}")
report.last(f"loop: {counts[0]} passed, {counts[1]} failed, {counts[2]} skipped in {loop['RUN']['seconds'](start)}; "
            "exit 0")
""" % SLEEP

# A failing build's log of a gate's form: 311 lines with the command's,
# 14 error lines, ninja's FAILED edge and clang's error each, 7 of each.
EDGES = (5, 51, 95, 138, 181, 224, 267)
BUILD = ("[print('FAILED: [code=1] o%d.o' % n if n in E else 'src/a.hpp:222:52: error: no matching constructor' "
         "if n - 5 in E else '  note %d' % n) for n in range(2, 312)]")


def scratch(case):
    path = Path(tempfile.mkdtemp(prefix="qiven3-loop-answer-"))
    case.addCleanup(shutil.rmtree, path, ignore_errors=True)
    return path


class Seconds(unittest.TestCase):
    def test_in_all(self):
        """Each step's seconds, their sum and the inner summary's within
        the seconds in all, though the inner run's monotonic clock runs
        at twice the host's; the answer at its bound, none dropped."""
        top = scratch(self)
        (top / "inner.py").write_text(INNER, encoding="utf-8")
        report = LOOP.Report(LOOP.LINES)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            begun = LOOP.RUN["now"]()
            for text in ("loop: build linux: job", "loop: the pin check", "loop: snapshot"):
                report.line(text)
            inner = [sys.executable, "-I", str(top / "inner.py"), str(ROOT / "tools" / "loop.py"), str(FAST), str(top)]
            status = LOOP.LINUX["relay"](report, inner, begun)
        lines = out.getvalue().splitlines()
        self.assertEqual(status, 0, lines)
        steps = [float(s) for s in re.findall(r"^job sleep \d: pass in (\d+\.\d) s$", out.getvalue(), re.MULTILINE)]
        self.assertEqual(len(steps), 2, lines)
        last = re.fullmatch(r"loop: 2 passed, 0 failed, 0 skipped in (\d+\.\d) s; exit 0 "
                            r"\((\d+) lines left out by the bound\); (\d+\.\d) s in all", lines[-1])
        self.assertIsNotNone(last, lines[-1])
        summary, in_all = float(last.group(1)), float(last.group(3))
        self.assertTrue(all(s >= 0.8 * SLEEP for s in steps), steps)  # in WSL the sleep is by the slewed clock
        self.assertLessEqual(sum(steps), in_all + 0.05 * len(steps), (steps, lines[-1]))  # each rounded to 0.1
        self.assertLessEqual(summary, in_all, lines[-1])
        self.assertLessEqual(sum(steps), summary + 0.05 * len(steps), (steps, lines[-1]))
        self.assertEqual((len(lines), report.dropped), (LOOP.LINES, 0), lines[-3:])

    def test_fuzz(self):
        """loop_fuzz's seconds by the loop's clock, now(): a target that
        runs a second shows a second, though the monotonic clock, and in
        WSL every clock but the raw one, runs at twice the host's."""
        top = scratch(self)
        (top / "tests").mkdir()
        (top / "tests" / "fuzz_targets.txt").write_text("t\tbinary\tcorpus\tartifacts\n", encoding="utf-8")
        fuzz = runpy.run_path(str(ROOT / "tools" / "loop_fuzz.py"))
        host, origin = time.monotonic, time.monotonic()
        patches = [mock.patch.object(time, "monotonic", lambda: origin + (host() - origin) * FAST),
                   mock.patch.object(subprocess, "run", lambda *a, **k: (time.sleep(SLEEP), mock.Mock(returncode=0))[1])]
        if hasattr(time, "clock_gettime"):
            get, raw = time.clock_gettime, time.CLOCK_MONOTONIC_RAW
            fast = {c: get(c) for c in (time.CLOCK_MONOTONIC, raw)}
            patches.append(mock.patch.object(time, "clock_gettime", lambda c: get(c) if c == raw else
                                             fast.setdefault(c, get(c)) + (get(c) - fast[c]) * FAST))
        out = io.StringIO()
        with contextlib.ExitStack() as stack, contextlib.redirect_stdout(out):
            for patch in patches:
                stack.enter_context(patch)
            status = fuzz["main"]([str(top), "1"])
        self.assertEqual(status, 0, out.getvalue())
        found = re.search(r"^fuzz t: pass in (\d+\.\d) s$", out.getvalue(), re.MULTILINE)
        self.assertIsNotNone(found, out.getvalue())
        self.assertTrue(0.8 * SLEEP <= float(found.group(1)) < 1.5 * SLEEP, out.getvalue())


class Failing(unittest.TestCase):
    def run_jobs(self, jobs, budget):
        """jobs: [(job, [(step, program or None for a pass)])], each step
        after the one before, as a job's; the lines of the answer."""
        logs = scratch(self)
        report = LOOP.Report(budget)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            report.line("loop: sync")
            for job, steps in jobs:
                report.line(f"{job} tree: kept, its own")
                table = [LOOP.Step(name, (name,), "test", after=steps[i - 1][0] if i else None)
                         for i, (name, _) in enumerate(steps)]
                programs = dict(steps)

                def command(step, programs=programs):
                    program = programs[step.name]
                    return [sys.executable, "-I", "-c", "print('ok')" if program is None else
                            program + "; raise SystemExit(1)"]

                LOOP.run_steps(report, job, table, "test", command, logs / job, logs, dict(os.environ))
            report.line("loop: logs in the clone")
            report.last("summary")
        return out.getvalue().splitlines()

    def test_three_builds(self):
        """linux-clang's, linux-tsan's and linux-fuzz's builds failing at a
        gate of the six jobs, in a bound of 97 as the inner run's: each
        one's first 10 error lines, fuzz's too, and a tail each, within
        the bound."""
        build = f"E = {EDGES!r}; {BUILD}"
        jobs = [(job, [("configure", None), ("build", build if job in ("linux-clang", "linux-tsan", "linux-fuzz")
                                              else None), *[(s, None) for s in more]])
                for job, more in (("linux-gcc", ["fake"]), ("linux-clang", ["fake"]), ("linux-tsan", ["fake"]),
                                  ("linux-fuzz", ["fake", "fuzz"]), ("linux-plain", ["fake", "clean", "debugger"]),
                                  ("linux-msan", ["fake", "cxx"]))]
        lines = self.run_jobs(jobs, 97)
        self.assertLessEqual(len(lines), 97)
        self.assertEqual(lines[-1], "summary")
        for job in ("linux-clang", "linux-tsan", "linux-fuzz"):
            at = lines.index(f"{job} build: its first 10 of 14 error lines, before its tail:")
            self.assertEqual(lines[at + 1:at + 3], ["  ! 5: FAILED: [code=1] o5.o",
                                                    "  ! 10: src/a.hpp:222:52: error: no matching constructor"])
            tail = [x for x in lines if re.fullmatch(rf"{job} build: its last \d+ of 311 lines:", x)]
            self.assertEqual(len(tail), 1, lines)
            self.assertGreaterEqual(int(tail[0].split()[4]), 5, tail)
        self.assertEqual(sum(1 for x in lines if x.startswith("  ! ")), 30)

    def test_error_in_tail(self):
        """An error line within the last 30 shows in the tail alone; a
        failing step with no error line shows its tail."""
        jobs = [("job", [("a", "[print('line', i) for i in range(60)]; print('The following tests FAILED:')"),
                         ("b", None)]),
                ("other", [("a", "[print('line', i) for i in range(60)]")])]
        lines = self.run_jobs(jobs, LOOP.LINES)
        self.assertFalse(any(" error lines" in x for x in lines), lines)
        self.assertEqual(lines.count("job a: its last 30 of 62 lines:"), 1, lines)
        self.assertEqual(lines.count("other a: its last 30 of 61 lines:"), 1, lines)
        self.assertEqual(sum(1 for x in lines if x == "  | The following tests FAILED:"), 1, lines)
        self.assertEqual(len(lines), 2 + 3 + 2 + 2 * 31 + 1, lines)


if __name__ == "__main__":
    unittest.main()
