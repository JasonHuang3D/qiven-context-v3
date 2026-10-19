"""The CTest wiring: the Python side's skip code, one whole test id selected
by -k, and each test's labels and properties as the test presets select
them."""

import json
import re
import unittest
from pathlib import Path

from tests import SKIP_CODE
from tests.drive import binaries

LABELS = {"cxx", "py", "fast", "fake", "slow", "machine", "drive", "oracle",
          "fuzz", "bench", "drill", "gpu"}
SAMPLE = "tests.drive.samples."
SELECT = "tests.drive.test_wiring.Select."


class Select(unittest.TestCase):
    """Two ids, one the other's prefix: a substring selects both."""

    def test_a(self):
        self.assertTrue(True)

    def test_ab(self):
        self.assertTrue(True)


def ran_count(ran):
    found = re.search(rb"^Ran (\d+) tests?", ran.err, re.MULTILINE)
    return int(found.group(1)) if found else None


def samples(*ids):
    args = ["discover", "-s", "tests", "-t", ".", "-p", "samples.py"]
    for one in ids:
        args += ["-k", "*" + SAMPLE + one]
    return binaries.unittest_run(*args)


class PythonSide(unittest.TestCase):
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
    def test_properties(self):
        tests = ctest_tests()
        self.assertIn("runner.pass_status", tests)
        self.assertIn(SELECT + "test_ab", tests)
        self.assertFalse([name for name in tests if name.startswith(("demo.", SAMPLE))])
        for name, (command, props) in tests.items():
            with self.subTest(name=name):
                labels = set(props["LABELS"])
                self.assertLessEqual(labels, LABELS)
                self.assertEqual(len(labels & {"cxx", "py"}), 1)
                self.assertEqual(props["SKIP_RETURN_CODE"], SKIP_CODE)
                if "py" in labels:
                    self.assertEqual(
                        command[1:],
                        ["-I", "-m", "unittest", "discover", "-s", "tests", "-t", ".", "-k", "*" + name],
                    )
                    self.assertEqual(Path(props["WORKING_DIRECTORY"]), binaries.ROOT)
                    self.assertIn("drive", labels)
                else:
                    self.assertEqual(command[1:], ["--run", name])
                self.assertEqual(props.get("RUN_SERIAL", False), name == "runner.registry_holds_self")

    def test_presets(self):
        tests = ctest_tests()
        presets = json.loads((binaries.ROOT / "CMakePresets.json").read_bytes())
        filters = {p["name"]: p.get("filter", {}) for p in presets["testPresets"]}
        everything = set(tests)
        expected = {
            "full": everything,
            "fast": labelled(tests, "fast"),
            "asan": labelled(tests, "cxx"),
        }
        self.assertIn("runner.pass_status", expected["fast"])
        self.assertNotIn("tests.drive.test_wiring.Registered.test_presets", expected["fast"])
        self.assertTrue(labelled(tests, "py") and labelled(tests, "cxx"))
        for preset, names in expected.items():
            with self.subTest(preset=preset):
                args = []
                include = filters[preset].get("include", {}).get("label")
                exclude = filters[preset].get("exclude", {}).get("label")
                if include:
                    args += ["-L", include]
                if exclude:
                    args += ["-LE", exclude]
                self.assertEqual(set(ctest_tests(*args)), names)
