"""The debugger agents can drive, tools/debug.py: K16, the transcript on
qiven3-fault's plant `watch`, which writes a counter three times in
plant_watch: the break, the watch's stops, the locals with the counter, a
stack frame plant_watch, within the bounds; its control, --lines 5, the
transcript cut at 5 lines with its last line. K17, debug.py dump on K1's
minidump naming plant_null, and on Linux on K8's core, written by the
watcher's hang path, naming plant_hang; its control, a file that is no
dump, exit 2. And the wrapper's other ends: a fault's, a case's, the time
bound's, a bad argument's.

On Windows the debugger is cdb.exe, pinned in tools/windows-pins.txt, and
it needs the binary's PDB beside it: a build that keeps none (Release,
whose binaries the clean-binaries check reads) skips the cases that name
a function. On Linux it is gdb, pinned in tools/linux-pins.txt, on
linux-plain's build, which has its debug information. Where the debugger
is absent, each case skips by name, "no cdb" or "no gdb", with the
runner's 125; one off its pin fails.
Under a sanitizer, whose runtime takes the process's faults, they are
skipped too.

Every case here drives the debugger: the label debugger, never fake, so
the loop runs them and CI never does.
"""

import re
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.test_faults import FAULT, LINE_POINTS, sanitized
from tools import debuggers

WINDOWS = sys.platform == "win32"
DEBUG = Path(__file__).resolve().parents[2] / "tools" / "debug.py"
SOURCE = Path(__file__).resolve().parents[1] / "faults" / "fault.cpp"
LAST = re.compile(r"^debug: (?P<outcome>.+), (?P<stops>\d+) stops, (?P<lines>\d+) lines, (?P<seconds>[\d.]+) s$")
COUNTER = re.compile(r"\bcounter = (?P<value>\S+)$")
STOP = re.compile(r"^stop (?P<n>\d+): ")
# A function's frame: cdb's "<module>!<name>+0x..", gdb's "<name> (...)".
FRAME = r"[!\s]{}[+(\s]"
DEFAULT_LINES = 200


def debug(*args, timeout=120):
    """Runs debug.py: its exit code and its lines."""
    done = subprocess.run([sys.executable, "-I", str(DEBUG), *map(str, args)], capture_output=True,
                          stdin=subprocess.DEVNULL, timeout=timeout, check=False)
    return done.returncode, done.stdout.decode("utf-8", "replace").splitlines()


def break_line():
    """fault.cpp's line of the watch plant's first write."""
    lines = SOURCE.read_text(encoding="utf-8").splitlines()
    return next(i + 1 for i, line in enumerate(lines) if "// the break's line" in line)


def by_stop(lines):
    """The transcript's lines by stop, from each stop's line."""
    out = []
    for line in lines:
        if STOP.match(line):
            out.append([line])
        elif out:
            out[-1].append(line)
    return out


class Debug(unittest.TestCase):
    ctest_labels = ("debugger",)

    def setUp(self):
        if sanitized():
            self.skipTest("under a sanitizer its runtime takes the faults")
        try:
            debuggers.find()
        except debuggers.Absent as e:
            self.skipTest(str(e))
        except debuggers.NotFound as e:
            self.fail(f"the debugger: {e}")
        self.fault = binaries.path(FAULT)

    def symbols(self):
        if WINDOWS and not self.fault.with_suffix(".pdb").is_file():
            self.skipTest("the build keeps no PDB beside its binaries: no function to name")

    def bounded(self, lines, bound=DEFAULT_LINES):
        self.assertLessEqual(len(lines), bound, lines)
        self.assertTrue(all(len(line) <= LINE_POINTS for line in lines), lines)
        last = LAST.match(lines[-1] if lines else "")
        self.assertIsNotNone(last, lines)
        self.assertEqual(int(last["lines"]), len(lines), lines)
        return last

    def watch(self, *bounds):
        return debug("run", "--break", f"fault.cpp:{break_line()}", "--watch", "counter", "--locals", "--stack",
                     *bounds, "--", self.fault, "watch")

    def test_K16_watch(self):
        self.symbols()
        code, lines = self.watch()
        last = self.bounded(lines)
        self.assertEqual(code, 0, lines)
        self.assertEqual(last["outcome"], "stop bound", lines)
        self.assertEqual(int(last["stops"]), 5, lines)
        self.assertEqual(lines[0], f"> break fault.cpp:{break_line()}", lines)
        stops = by_stop(lines[1:-1])
        self.assertEqual(len(stops), 5, lines)
        self.assertIn("> watch counter", stops[0], lines)
        # The watch's stops, each after a write, the counter's value in the locals.
        values = []
        for stop in stops[1:4]:
            # The locals' lines alone: gdb's source lines name the counter too.
            at = stop.index("> locals") + 1
            locals_ = stop[at:next((i for i in range(at, len(stop)) if stop[i].startswith("> ")), len(stop))]
            found = [COUNTER.search(line) for line in locals_]
            values += [m["value"] for m in found if m]
            self.assertTrue(any(re.search(FRAME.format("plant_watch"), line) for line in stop), stop)
            self.assertLess(stop.index("> locals"), stop.index("> stack"), stop)
        self.assertEqual(values, ["1", "2", "3"], lines)

    def test_K16_control(self):
        self.symbols()
        code, lines = self.watch("--lines", "5")
        self.bounded(lines, 5)
        self.assertEqual(len(lines), 5, lines)
        self.assertEqual(lines[-2], debuggers.CUT, lines)
        self.assertEqual(code, 0, lines)

    def test_K17_dump(self):
        self.symbols()
        if not WINDOWS:
            return self.core()
        from tests.drive.test_faults import run_plant
        dumps = binaries.directory("QIVEN3_BUILD") / "dumps"
        _, _, err, _, _ = run_plant(FAULT, "null")
        name = next(line.removeprefix("fatal: dump ") for line in err.decode("utf-8", "replace").splitlines()
                    if line.startswith("fatal: dump "))
        code, lines = debug("dump", dumps / name, "--symbols", self.fault.parent)
        last = self.bounded(lines)
        self.assertEqual(code, 1, lines)
        self.assertTrue(last["outcome"].startswith("fault Access violation - code c0000005"), lines)
        self.assertTrue(any(re.search(r"!plant_null\+", line) for line in lines), lines)
        for action in ("> context", "> stack", "> locals", "> threads"):
            self.assertIn(action, lines)

    def core(self):
        """Linux: K8's core, the watcher's, read: no fault, both threads'
        stacks holding plant_hang."""
        from tests.drive.test_hang import IN_PLANT, LIMIT, run_watched, sections
        start = time.time()
        status, _, err, _, _ = run_watched([self.fault, "hang"], "K17", LIMIT)
        self.assertEqual(status, 124, err)
        dumps = binaries.directory("QIVEN3_BUILD") / "dumps"
        written = [p for p in dumps.glob(f"{FAULT}-*-hang.core") if p.stat().st_mtime >= start - 1
                   and any(line.startswith("Saved corefile ") and line.endswith(p.name) for line in err)]
        self.assertEqual(len(written), 1, (written, err))
        code, lines = debug("dump", written[0], "--symbols", self.fault.parent)
        last = self.bounded(lines)
        self.assertEqual((code, last["outcome"]), (0, "dump read"), lines)
        self.assertEqual(lines[0], "dump: no event", lines)
        for action in ("> context", "> stack", "> locals", "> threads"):
            self.assertIn(action, lines)
        threads = sections(lines[lines.index("> threads") + 1:-1])
        self.assertGreaterEqual(sum(any(IN_PLANT.search(line) for line in s) for s in threads), 2, lines)
        self.assertFalse(any(re.search(FRAME.format("plant_null"), line) for line in lines), lines)

    def test_K17_control(self):
        with tempfile.TemporaryDirectory() as scratch:
            fake = Path(scratch) / "fault.dmp"
            fake.write_bytes(b"no dump at all\n")
            code, lines = debug("dump", fake)
        self.bounded(lines)
        self.assertEqual(code, 2, lines)
        self.assertRegex(lines[-1], r"^debug: could not run: .* is no (minidump|core file), 0 stops, 1 lines, ")

    def test_fault(self):
        self.symbols()
        code, lines = debug("run", "--stack", "--", self.fault, "null")
        last = self.bounded(lines)
        self.assertEqual(code, 1, lines)
        self.assertEqual(int(last["stops"]), 1, lines)
        self.assertTrue(any(re.search(FRAME.format("plant_null"), line) for line in lines), lines)
        if not WINDOWS:
            self.assertEqual(last["outcome"], "fault SIGSEGV, Segmentation fault", lines)

    def test_exited(self):
        code, lines = debug("run", "--", self.fault, "out")
        self.assertEqual(self.bounded(lines)["outcome"], "exited 0 passed", lines)
        self.assertEqual(code, 0, lines)
        code, lines = debug("run", "--", self.fault, "nothing")
        self.assertEqual(self.bounded(lines)["outcome"], "exited 2 unnamed", lines)
        self.assertEqual(code, 0, lines)

    def test_timed_out(self):
        code, lines = debug("run", "--seconds", "2", "--", self.fault, "hang", timeout=60)
        last = self.bounded(lines)
        self.assertEqual((code, last["outcome"]), (1, "timed out"), lines)
        self.assertLess(float(last["seconds"]), 10, lines)

    def test_case(self):
        """case reads the test's command from the build's list, the watcher
        dropped: the runner's own test binary, to its end."""
        build = binaries.directory("QIVEN3_BUILD")
        code, lines = debug("case", "runner.counted_held", "--build", build)
        self.assertEqual(self.bounded(lines)["outcome"], "exited 0 passed", lines)
        self.assertEqual(code, 0, lines)
        code, lines = debug("case", "no.such_test", "--build", build)
        self.assertEqual(code, 2, lines)

    def test_bad_arguments(self):
        for args in (["run", "--lines", "1001", "--", self.fault], ["run", "--seconds", "601", "--", self.fault],
                     ["run", "--lines", "1", "--", self.fault], ["run"], ["dump"],
                     ["run", "--", Path(self.fault).with_name("no-such-binary")]):
            code, lines = debug(*args)
            self.bounded(lines)
            self.assertEqual(code, 2, (args, lines))
            self.assertIn("could not run: ", lines[-1])


if __name__ == "__main__":
    unittest.main()
