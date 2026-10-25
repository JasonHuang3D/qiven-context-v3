"""The CTest wiring: the Python side's skip code, one whole test id selected
by -k, and each test's labels and properties as the test presets select
them: each behind the watcher, under its limit by its labels (a slow test
1,800 s, a fast one 60 s, any other 300 s) or its own, CTest's TIMEOUT 30 s
past it."""

import json
import re
import unittest
from pathlib import Path

from tests import SKIP_CODE
from tests.drive import binaries

LABELS = {"cxx", "py", "fast", "fake", "debugger", "slow", "machine", "drive", "oracle",
          "fuzz", "bench", "drill", "gpu", "analysis"}
# The structure tests start no process and read no machine fact: fast, and
# not drive; but the analysis tools' controls, which run the tools on the
# analysis build: analysis alone. Every other Python test drives a binary.
STRUCTURE = "tests.structure."
ANALYSIS = "tests.structure.test_analysis."
SAMPLE = "tests.drive.samples."
SELECT = "tests.drive.test_wiring.Select."
# A fuzz target's run, under libFuzzer alone: fuzz.<target>.
FUZZ = "fuzz."
# The watcher's limits by label, and CTest's seconds past them.
SLOW_LIMIT, FAST_LIMIT, OTHER_LIMIT, AFTER_LIMIT = 1800, 60, 300, 30


class Select(unittest.TestCase):
    """Two ids, one the other's prefix: a substring selects both."""
    ctest_labels = ("fake",)

    def test_a(self):
        self.assertTrue(True)

    def test_ab(self):
        self.assertTrue(True)


def serial(name):
    """Whether a test runs alone: the runner's registry case, each fuzz
    target's run, and each Python test whose class sets ctest_serial."""
    if name == "runner.registry_holds_self" or name.startswith(FUZZ):
        return True
    if not name.startswith("tests."):
        return False
    test = next(iter(unittest.defaultTestLoader.loadTestsFromName(name)))
    return bool(getattr(test, "ctest_serial", False))


def limit_of(name, labels, command, cases):
    """A test's limit: its own, a Python test's class's ctest_limit or a C++
    case's binary's LIMIT (`cases`, case_limits()'s), else its labels'."""
    if name.startswith("tests."):
        own = getattr(next(iter(unittest.defaultTestLoader.loadTestsFromName(name))), "ctest_limit", None)
    else:
        own = cases.get(Path(command[0]).resolve())
    if own is not None:
        return own
    if "slow" in labels:
        return SLOW_LIMIT
    return FAST_LIMIT if "fast" in labels else OTHER_LIMIT


def case_limits():
    """Each case binary's own limit, by its file, from the build's listing
    inputs (tests/case_binaries.txt), where it states one."""
    found = {}
    inputs = binaries.directory("QIVEN3_BUILD") / "tests" / "case_binaries.txt"
    for line in inputs.read_text(encoding="utf-8").splitlines():
        _, limit, binary = line.split("\t")
        if limit:
            found[Path(binary).resolve()] = int(limit)
    return found


def ran_count(ran):
    found = re.search(rb"^Ran (\d+) tests?", ran.err, re.MULTILINE)
    return int(found.group(1)) if found else None


def samples(*ids):
    args = ["discover", "-s", "tests", "-t", ".", "-p", "samples.py"]
    for one in ids:
        args += ["-k", "*" + SAMPLE + one]
    return binaries.unittest_run(*args)


class PythonSide(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_skip_code(self):
        cases = [
            (("Samples.test_passes",), 0, 1),
            (("Samples.test_fails",), 1, 1),
            (("Samples.test_skipped",), SKIP_CODE, 1),
            (("SkippedClass.test_any",), SKIP_CODE, 1),
            (("Samples.test_passes", "Samples.test_skipped"), 0, 2),
            (("Samples.test_fails", "Samples.test_skipped"), 1, 2),
            (("Samples.test_none",), 5, 0),
        ]
        for ids, status, count in cases:
            with self.subTest(ids=ids):
                ran = samples(*ids)
                self.assertEqual(ran.status, status, ran.err.decode("utf-8", "replace"))
                self.assertEqual(ran_count(ran), count)

    def test_whole_id(self):
        common = ["discover", "-s", "tests", "-t", "."]
        whole = binaries.unittest_run(*common, "-k", "*" + SELECT + "test_a")
        self.assertEqual(whole.status, 0, whole.err.decode("utf-8", "replace"))
        self.assertEqual(ran_count(whole), 1)
        part = binaries.unittest_run(*common, "-k", SELECT + "test_a")
        self.assertEqual(part.status, 0, part.err.decode("utf-8", "replace"))
        self.assertEqual(ran_count(part), 2)


def ctest_tests(*args):
    """This build's tests, by ctest -N: each name to its command and properties."""
    ran = binaries.run_file(
        binaries.tool("QIVEN3_CTEST"),
        ["--test-dir", binaries.directory("QIVEN3_BUILD"), "-N", "--show-only=json-v1", *args],
    )
    if ran.status != 0:
        raise AssertionError(ran.err.decode("utf-8", "replace"))
    found = {}
    for test in json.loads(ran.out)["tests"]:
        props = {p["name"]: p["value"] for p in test.get("properties", [])}
        found[test["name"]] = (test["command"], props)
    return found


def labelled(tests, label):
    return {name for name, (_, props) in tests.items() if label in props.get("LABELS", [])}


class Registered(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_properties(self):
        tests = ctest_tests()
        self.assertIn("runner.pass_status", tests)
        self.assertIn(SELECT + "test_ab", tests)
        self.assertFalse([name for name in tests if name.startswith(("demo.", SAMPLE))])
        watch = binaries.ROOT / "tools" / "watch.py"
        cases = case_limits()
        for name, (watched, props) in tests.items():
            with self.subTest(name=name):
                labels = set(props["LABELS"])
                self.assertLessEqual(labels, LABELS)
                self.assertEqual(len(labels & {"cxx", "py"}), 1)
                self.assertEqual(props["SKIP_RETURN_CODE"], SKIP_CODE)
                # Behind the watcher, under its limit; then the test's own command.
                self.assertEqual(watched[1:4], ["-I", "-X", "faulthandler"])
                self.assertEqual(Path(watched[4]), watch)
                self.assertEqual([watched[5], watched[7], watched[8], watched[9]], ["--limit", "--name", name, "--"])
                command = watched[10:]
                limit = limit_of(name, labels, command, cases)
                self.assertEqual(int(watched[6]), limit)
                self.assertEqual(float(props["TIMEOUT"]), limit + AFTER_LIMIT)
                if "py" in labels:
                    self.assertEqual(
                        command[1:],
                        ["-I", "-m", "unittest", "discover", "-s", "tests", "-t", ".", "-k", "*" + name],
                    )
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
                self.assertEqual(props.get("RUN_SERIAL", False), serial(name))

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
            "structure": {name for name in tests if name.startswith(STRUCTURE)} - analysis,
            "analysis": analysis,
            "fake": labelled(tests, "fake"),
            "debugger": labelled(tests, "debugger"),
        }
        self.assertTrue(analysis and all(name.startswith(ANALYSIS) for name in analysis))
        # The debugger cases, the loop's alone, never fake, CI's.
        self.assertIn("tests.drive.test_hang.HangStacks.test_K8_hang", expected["debugger"])
        self.assertEqual(expected["fake"] & expected["debugger"], set())
        self.assertIn("runner.pass_status", expected["fast"])
        self.assertNotIn("tests.drive.test_wiring.Registered.test_presets", expected["fast"])
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
