"""The toolchain as the loop's input, and what its answer names. Each kept
tree records the toolchain's pins it was configured under: in a scratch
checkout the case makes, a planted pin change empties the tree, a file
planted in it gone, and its fake step runs every test, its line saying
why; an unchanged pin keeps the tree and the narrow selection; a tree whose
mark records no pins is emptied once. Each system's side gives its pins:
Windows' table, tools/windows-pins.txt and CMakeLists.txt's; inside WSL
the pin file's, CMakeLists.txt's and the prefix, to every unit it drives.
A ctest step's status is named as ctest's, 8 as (tests failed); another
step's 8 stays unnamed. Inside WSL each sanitizer runtime's symbolizer is
the pinned file, and no GCC installation is given; --help names each
symbolizer variable."""

import argparse
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive.test_loop_ci import ROOT, load_loop
from tests.drive.test_loop_select import TREE, built, change, checkout, selection

LOOP = load_loop()
RUN = LOOP.RUN
STATUS = RUN["STATUS"]
JOB = "windows-msvc"
PINS = {"cl": "14.44.35207 one", "QIVEN3_PIN_GCC": "13.3.0", "command g++-13": "13.3.0 two path/g++-13"}


def scratch(case):
    path = Path(tempfile.mkdtemp(prefix="qiven3-loop-pins-"))
    case.addCleanup(shutil.rmtree, path, ignore_errors=True)
    return path


class KeptTree(unittest.TestCase):
    """Tier 1's windows-msvc unit on fakes in a scratch checkout, its tree
    owned under the pins each run gives."""
    ctest_labels = ("fake",)

    def setUp(self):
        self.root = checkout(self)
        self.tree = self.root / TREE

    def run_tier(self, pins):
        """The unit run under pins: its fake step's argv, or None when it
        did not run; its tree's line and its fake step's line."""
        unit = LOOP.plan("1", "windows", [JOB])[0]
        unit.select = selection(self.root)
        presets = LOOP.binary_dirs()
        ran = {}

        def command(unit, step):
            argv = LOOP.local_argv(unit, step, presets)
            if step.name == "build":
                built(self.tree)
            if argv is None:
                return None
            ran[step.name] = argv
            return [sys.executable, "-I", "-c", "raise SystemExit(0)"]

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            counts = LOOP.drive(LOOP.Report(LOOP.LINES), [unit], self.root, self.root / "build" / "loop", command,
                                dict(os.environ), "test", pins)
        lines = out.getvalue().splitlines()
        self.assertEqual(counts[1], 0, lines)
        tree = [x for x in lines if x.startswith(f"{JOB} tree:")][0]
        fake = [x for x in lines if x.startswith(f"{JOB} fake:")][0]
        return ran.get("fake"), tree, fake

    def mark(self):
        return RUN["marked"](self.tree / RUN["TREE_MARK"])

    def test_pin_change(self):
        """The plant: one pin changed empties the tree and runs every test,
        both lines naming the pin; the same pins after keep the tree, and a
        change narrows the step again."""
        self.run_tier(PINS)
        argv, tree, fake = self.run_tier(PINS)
        self.assertIsNone(argv)
        self.assertEqual(tree, f"{JOB} tree: {self.tree} kept, its own")
        self.assertIn("0 of 10 tests: nothing it runs changed", fake)
        self.assertEqual(self.mark(), (JOB, PINS))
        planted = self.tree / "CMakeCache.txt"
        planted.write_text("made by another compiler\n", encoding="utf-8")
        repinned = dict(PINS, **{"command g++-13": "13.4.0 three path/g++-13"})
        change(self.root, "src/utc/utc.cpp", "// utc, changed\n")
        argv, tree, fake = self.run_tier(repinned)
        self.assertEqual(tree, f"{JOB} tree: {self.tree} emptied, the toolchain's pins changed: command g++-13")
        self.assertFalse(planted.exists())
        self.assertNotIn("--tests-from-file", argv)
        self.assertIn("every test, 10 of 10: its tree emptied, the toolchain's pins changed: command g++-13", fake)
        self.assertEqual(self.mark(), (JOB, repinned))
        change(self.root, "src/utc/utc.cpp", "// utc, changed again\n")
        argv, tree, fake = self.run_tier(repinned)
        self.assertEqual(tree, f"{JOB} tree: {self.tree} kept, its own")
        self.assertIn("--tests-from-file", argv)
        self.assertIn("2 of 10 tests: utc changed", fake)

    def test_no_pins_recorded(self):
        """A mark of the job's with no pins, an earlier loop's: the tree is
        emptied once, every test run, then kept."""
        self.run_tier(None)
        self.assertEqual(self.mark(), (JOB, {}))
        argv, tree, fake = self.run_tier(PINS)
        self.assertEqual(tree, f"{JOB} tree: {self.tree} emptied, it records no toolchain pins")
        self.assertIn("every test, 10 of 10: its tree emptied, it records no toolchain pins", fake)
        self.assertEqual(self.run_tier(PINS)[1], f"{JOB} tree: {self.tree} kept, its own")

    def test_names_shown(self):
        """A tree's line names three changed pins at most, then counts the
        rest; a pin gone or new counts as changed."""
        had = {f"p{i}": "1" for i in range(6)}
        now = dict(had, p0="2", p1="2", p2="2", p6="1")
        del now["p5"]
        self.assertEqual(RUN["pins_changed"](had, now), "the toolchain's pins changed: p0, p1, p2 and 2 more")
        self.assertIsNone(RUN["pins_changed"](had, dict(had)))
        self.assertIsNone(RUN["pins_changed"](had, None))


class SystemPins(unittest.TestCase):
    """Each system's side: the pins its trees record."""
    ctest_labels = ("fake",)

    def test_windows(self):
        """Windows' table, the toolset, the SDK, CPython, clang-tidy, the
        debugger's pin file and CMakeLists.txt's pins; a pin file's change
        is a change of its pins."""
        top = scratch(self)
        (top / "tools").mkdir()
        shutil.copyfile(ROOT / "tools" / "windows-pins.txt", top / "tools" / "windows-pins.txt")
        shutil.copyfile(ROOT / "CMakeLists.txt", top / "CMakeLists.txt")
        pins = LOOP.WINDOWS["windows_pins"](top)
        for name in [*(p[0] for p in LOOP.WINDOWS_PINS), "toolset", "sdk", "cpython", "clang-tidy", "debugger cdb",
                     "QIVEN3_PIN_MSVC", "QIVEN3_PIN_GCC", "QIVEN3_PIN_CLANG", "QIVEN3_PIN_CMAKE"]:
            self.assertIn(name, pins)
        self.assertEqual(pins["cpython"], "3.15.0")
        text = (top / "tools" / "windows-pins.txt").read_text(encoding="utf-8")
        (top / "tools" / "windows-pins.txt").write_text(text.replace("debugger cdb 10.", "debugger cdb 11."),
                                                        encoding="utf-8")
        self.assertEqual(RUN["pins_changed"](pins, LOOP.WINDOWS["windows_pins"](top)),
                         "the toolchain's pins changed: debugger cdb")

    def test_pin_file(self):
        """A pin file's pins by kind and name, the comments apart: the
        Linux file's every pin, and none of its removed packages, which
        no build uses: a removal's line is no pin a tree records."""
        text = (ROOT / "tools" / "linux-pins.txt").read_text(encoding="utf-8")
        pins = RUN["file_pins"](text)
        lines = [x.split("#", 1)[0].split() for x in text.splitlines() if x.strip() and not x.startswith("#")]
        removed = [f for f in lines if f[0] == "removed"]
        self.assertTrue(removed and all(len(f) == 2 for f in removed), removed)
        self.assertEqual(len(pins), len(lines) - len(removed))
        self.assertEqual([k for k in pins if k.split()[0] == "removed"], [])
        self.assertTrue(all("#" not in v for v in pins.values()), pins)
        self.assertIn("command g++-13", pins)

    def test_inside(self):
        """Inside WSL, every unit driven under the pin file's pins,
        CMakeLists.txt's and the prefix; the sync and the drive faked."""
        base = scratch(self)
        linux = LOOP.LINUX["inside"].__globals__
        given, envs = [], []

        def drive(report, units, root, logs, command, env, phase="test", pins=None):
            given.append(pins)
            envs.append(env)
            return [1, 0, 0]

        saved = {k: linux[k] for k in ("sync", "drive")}
        linux["sync"] = lambda git, clone, source, commit: None
        linux["drive"] = drive
        jobs = [n for n, job in LOOP.JOBS.items() if job[0] == "linux"]
        args = argparse.Namespace(way="2", jobs=jobs, fuzz_time=None, pins=None, prefix="/nonexistent/prefix/",
                                  clone=str(base), source=str(base), commit="0" * 40)
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                linux["inside"](args, LOOP.Report(LOOP.LINES))
        finally:
            linux.update(saved)
        self.assertEqual(len(given), len(jobs))
        expected = {**RUN["file_pins"]((ROOT / "tools" / "linux-pins.txt").read_text(encoding="utf-8")),
                    **RUN["cmake_pins"](ROOT), "prefix": "/nonexistent/prefix"}
        self.assertTrue(all(pins == expected for pins in given), given[0])
        self.assertIn("QIVEN3_PIN_GCC", expected)
        # Each runtime's symbolizer the pinned file, by its pin's source (none where it is not found, as
        # on Windows); no GCC installation given, clang taking the system's only GCC.
        source = [v.split()[-1] for k, v in expected.items() if k.startswith("command llvm-symbolizer")]
        self.assertEqual(len(source), 1, expected)
        for env in envs:
            symbolizer = linux["pinned_file"](source[0], "/nonexistent/prefix", env["PATH"])
            self.assertEqual({env.get(r + "_SYMBOLIZER_PATH") for r in LOOP.LINUX["SANITIZER_RUNTIMES"]},
                             {symbolizer})
            self.assertNotIn("QIVEN3_GCC", env)

    def test_symbolizer(self):
        """A command pin's file: prefix/PATH under the prefix, path/NAME by
        the PATH given (a planted file, on POSIX, where a bare name is a
        command), None for another form or a name not found."""
        pinned_file = LOOP.LINUX["pinned_file"]
        self.assertEqual(pinned_file("prefix/bin/llvm-symbolizer-18", "/p", ""), "/p/bin/llvm-symbolizer-18")
        self.assertIsNone(pinned_file("usr/bin/llvm-symbolizer-18", "/p", ""))
        bin_dir = scratch(self)
        self.assertIsNone(pinned_file("path/llvm-symbolizer-18", "/p", str(bin_dir)))
        if os.name == "posix":
            planted = bin_dir / "llvm-symbolizer-18"
            planted.write_bytes(b"#!/bin/sh\n")
            planted.chmod(0o755)
            self.assertEqual(pinned_file("path/llvm-symbolizer-18", "/p", str(bin_dir)), str(planted))


class Statuses(unittest.TestCase):
    """A failing step's status, as its line names it."""
    ctest_labels = ("fake",)

    def fail_line(self, tool, code):
        top = scratch(self)
        step = LOOP.Step("step", (tool, "--x"), "test", after=None)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            counts = LOOP.run_steps(LOOP.Report(LOOP.LINES), "job", [step], "test",
                                    lambda step: [sys.executable, "-I", "-c", f"raise SystemExit({code})"],
                                    top / "logs", top, None)
        self.assertEqual(counts, [0, 1, 0])
        return [x for x in out.getvalue().splitlines() if x.startswith("job step: FAIL")][0]

    def test_ctest_status(self):
        """ctest's 8 by ctest's name; another tool's 8 unnamed."""
        self.assertIn("FAIL, ctest's status: 8 (tests failed): a test failed or timed out, or none ran under "
                      "--no-tests=error, in", self.fail_line("ctest", 8))
        self.assertIn("FAIL, status: 8 unnamed, in", self.fail_line("cmake", 8))

    def test_ctest_line(self):
        """ctest's table, its unnamed statuses, and a death by a signal or a
        system status named as the process's."""
        self.assertEqual(STATUS["ctest_line"](1), "ctest's status: 1 (error): ctest could not run: an argument, "
                                                  "a preset or a test directory it cannot take")
        self.assertEqual(STATUS["ctest_line"](5), "ctest's status: 5 unnamed")
        for status in (-11, 0xC0000005):
            self.assertEqual(STATUS["ctest_line"](status), STATUS["line"](status))


class Help(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_help(self):
        """--help names QIVEN3_LIBCXX and each sanitizer runtime's
        symbolizer variable, and no GCC installation the loop gives."""
        done = subprocess.run([sys.executable, "-I", str(ROOT / "tools" / "loop.py"), "--help"],
                              stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
        text = " ".join(done.stdout.decode("utf-8", "replace").split())
        self.assertEqual(done.returncode, 0, text[-400:])
        names = ["QIVEN3_LIBCXX", *(r + "_SYMBOLIZER_PATH" for r in LOOP.LINUX["SANITIZER_RUNTIMES"])]
        for name in names:
            self.assertIn(name, text)
        for name in ("GCC_HOME", "QIVEN3_GCC", "EXTERNAL_TOOLCHAIN"):
            self.assertNotIn(name, text)
        self.assertIn("ctest's status: 8 (tests failed)", text)
        self.assertIn("each kept tree records the pins it was configured under", text)


if __name__ == "__main__":
    unittest.main()
