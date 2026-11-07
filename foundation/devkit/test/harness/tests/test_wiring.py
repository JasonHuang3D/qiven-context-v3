"""The CTest wiring: one whole test id run by Python's case entry, and each
test's labels and properties as the test presets select them: each labelled
with one module's name (the table, foundation/devkit/python/table.py), behind
the watcher, under its limit by its labels (a slow test 1,800 s, a fast one
60 s, any other 300 s), CTest's TIMEOUT 30 s past it, its skip its exit in
both languages and a fuzz target's run alone."""

import json
import re
import unittest
from pathlib import Path

from foundation.devkit.python import build_tree
from foundation.devkit.python import system
from foundation.devkit.python import table
from foundation.devkit.test.harness import binaries
from foundation.devkit.test.harness.binaries import SKIP_CODE

LABELS = {"cxx", "py", "fast", "fake", "debugger", "slow", "machine", "drive", "oracle",
          "fuzz", "bench", "drill", "gpu", "analysis"}
MODULES = {m["name"] for m in table.load(binaries.ROOT)["modules"]}
# The structure tests start no process but git and read no machine fact: fast,
# and not drive. The analysis tools' controls, build's, which run the tools on
# the analysis build: analysis alone. Every other Python test is labelled drive
# by its package, whether it drives a binary or reads a build's files.
STRUCTURE = "foundation.devkit.structure."
ANALYSIS = "foundation.devkit.build.tests.test_analysis."
SELECT = "foundation.devkit.test.harness.tests.test_wiring.Select."
# The Windows adapter's own file cases, on Windows alone.
WIN_FILES = "win_files."
# A fuzz target's run, under libFuzzer alone: fuzz.<target>.
FUZZ = "fuzz."
# The watcher's limits by label, and CTest's seconds past them.
SLOW_LIMIT, FAST_LIMIT, OTHER_LIMIT, AFTER_LIMIT = 1800, 60, 300, 30


class Select(unittest.TestCase):
    """Two ids, one the other's prefix: the case entry runs the shorter alone."""
    ctest_labels = ("fake",)

    def test_a(self):
        self.assertTrue(True)

    def test_ab(self):
        self.assertTrue(True)


def ran_count(ran):
    found = re.search(rb"^Ran (\d+) tests?", ran.err, re.MULTILINE)
    return int(found.group(1)) if found else None


class PythonSide(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_whole_id(self):
        whole = binaries.case_run(SELECT + "test_a")
        self.assertEqual(whole.status, 0, whole.err.decode("utf-8", "replace"))
        self.assertEqual(ran_count(whole), 1)


def ctest_tests(*args):
    """This build's tests, by ctest's listing (args a selection): each name to its command and properties."""
    return build_tree.tests(binaries.directory("QIVEN_BUILD"), args)


def labelled(tests, label):
    return {name for name, (_, props) in tests.items() if label in props.get("LABELS", [])}


class Registered(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_properties(self):
        tests = ctest_tests()
        self.assertIn("runner.pass_status", tests)
        self.assertIn(SELECT + "test_ab", tests)
        self.assertFalse([name for name in tests if name.startswith("demo.")])
        watch = binaries.WATCHER
        for name, (watched, props) in tests.items():
            with self.subTest(name=name):
                labels = set(props["LABELS"])
                self.assertEqual(len(labels & MODULES), 1, labels)  # its module's, by which the runner selects it
                labels -= MODULES - LABELS
                self.assertLessEqual(labels, LABELS)
                self.assertEqual(len(labels & {"cxx", "py"}), 1)
                self.assertEqual(props["SKIP_RETURN_CODE"], SKIP_CODE)
                self.assertNotIn("SKIP_REGULAR_EXPRESSION", props)
                # Behind the watcher, under its limit; then the test's own command.
                self.assertEqual(watched[1:4], ["-I", "-X", "faulthandler"])
                self.assertEqual(Path(watched[4]), watch)
                self.assertEqual([watched[5], watched[7], watched[8], watched[9]], ["--limit", "--name", name, "--"])
                command = watched[10:]
                limit = SLOW_LIMIT if "slow" in labels else FAST_LIMIT if "fast" in labels else OTHER_LIMIT
                self.assertEqual(int(watched[6]), limit)
                self.assertEqual(float(props["TIMEOUT"]), limit + AFTER_LIMIT)
                if "py" in labels:
                    self.assertEqual(command[1], "-I")
                    self.assertEqual(Path(command[2]), binaries.CASE)
                    self.assertEqual(command[3:], ["--skip", str(SKIP_CODE), name])
                    self.assertEqual(Path(props["WORKING_DIRECTORY"]), binaries.ROOT)
                    if name.startswith(ANALYSIS):
                        self.assertEqual(labels, {"py", "analysis"})
                    elif name.startswith(STRUCTURE):
                        self.assertIn("fast", labels)
                        self.assertNotIn("drive", labels)
                    else:
                        self.assertIn("drive", labels)
                elif "fuzz" in labels:
                    # libFuzzer's run from the target's corpus, its findings
                    # under its artifacts.
                    self.assertTrue(name.startswith(FUZZ))
                    self.assertEqual(labels, {"cxx", "fuzz"})
                    target = name[len(FUZZ):]
                    self.assertEqual(command[1:3], ["-max_total_time=60", "-timeout=10"])
                    self.assertTrue(command[3].startswith("-artifact_prefix=")
                                    and command[3].endswith(f"fuzz/{target}/artifacts/"))
                    self.assertEqual(Path(command[4]).parts[-3:], ("fuzz", target, "corpus"))
                else:
                    self.assertEqual(command[1:], ["--run", name])
                self.assertEqual(props.get("RUN_SERIAL", False), "fuzz" in labels)

    def test_presets(self):
        tests = ctest_tests()
        presets = json.loads((binaries.ROOT / "CMakePresets.json").read_bytes())
        filters = {p["name"]: p.get("filter", {}) for p in presets["testPresets"]}
        analysis = labelled(tests, "analysis")
        fuzz = labelled(tests, "fuzz")
        expected = {
            "full": set(tests) - analysis - fuzz,
            "fast": labelled(tests, "fast"),
            "asan": labelled(tests, "cxx") - fuzz,
            "fuzz": fuzz,
            "structure": {name for name in tests if name.startswith(STRUCTURE)},
            "analysis": analysis,
            "fake": labelled(tests, "fake"),
            "debugger": labelled(tests, "debugger"),
        }
        self.assertTrue(analysis and all(name.startswith(ANALYSIS) for name in analysis))
        # The debugger cases, the debugger preset's alone, never fake, CI's.
        self.assertIn("foundation.devkit.test.harness.tests.test_hang.HangStacks.test_K8_hang", expected["debugger"])
        self.assertEqual(expected["fake"] & expected["debugger"], set())
        self.assertIn("runner.pass_status", expected["fast"])
        # win/files' cases in the fake selection, which every Windows job
        # and CI's Windows jobs run.
        win_files = {name for name in tests if name.startswith(WIN_FILES)}
        self.assertEqual(bool(win_files), system.WINDOWS)
        self.assertLessEqual(win_files, expected["fake"] & expected["fast"])
        self.assertNotIn("foundation.devkit.test.harness.tests.test_wiring.Registered.test_presets", expected["fast"])
        self.assertTrue(labelled(tests, "py") and labelled(tests, "cxx"))
        for preset, names in expected.items():
            with self.subTest(preset=preset):
                args = []
                for option, kind, key in (("-L", "include", "label"), ("-LE", "exclude", "label"),
                                          ("-R", "include", "name"), ("-E", "exclude", "name")):
                    value = filters[preset].get(kind, {}).get(key)
                    if value:
                        args += [option, value]
                self.assertEqual(set(ctest_tests(*args)), names)
