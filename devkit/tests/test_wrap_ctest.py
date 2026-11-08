"""CTest's answers decoded whole, against a real CTest on a CTestTestfile.cmake
of the test's own, with no configure: a test's name, command or property
misread, a result misread, or a name CTest dropped read as a pass, would put
wrong data into what devkit's store keeps."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

from qiven_devkit.wrap import call, ctest

LIMIT = 120
ODD = "caf\N{LATIN SMALL LETTER E WITH ACUTE} \N{CJK UNIFIED IDEOGRAPH-4E2D}"
MARKED = 'a<b&"c">'


def word(text):
    """text as a CMake bracket argument."""
    return f"[=[{text}]=]"


@unittest.skipUnless(sys.platform == "win32", "wrap runs on Windows only")
class Answers(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.tree = self.root / "tree"
        self.tree.mkdir()
        env = dict(os.environ)
        # cmake by the case of its name on disk, as CTest lists it
        self.cmake = Path(os.path.realpath(call.locate("test", "cmake", env))).as_posix()
        here = self.tree.as_posix()
        tests = {
            "pass": [self.cmake, "-E", "echo", "hi"],
            "fail": [self.cmake, "-E", "false"],
            # cmd by its bare name: given a path with forward slashes, it reads one as its own switch
            "skip": [self.cmake, "-E", "env", "cmd", "/c", "exit 125"],
            "slow": [self.cmake, "-E", "sleep", "5"],
            "gone": [(self.root / "no-such-program.exe").as_posix()],
            ODD: [self.cmake, "-E", "true"],
            MARKED: [self.cmake, "-E", "true"],
        }
        lines = [f"add_test({word(name)} {' '.join(word(w) for w in command)})" for name, command in tests.items()]
        lines += [f"set_tests_properties({word(name)} PROPERTIES WORKING_DIRECTORY {word(here)})" for name in tests]
        lines += [f'set_tests_properties({word("pass")} PROPERTIES LABELS "{ODD};x" TIMEOUT "50")',
                  f'set_tests_properties({word("skip")} PROPERTIES SKIP_RETURN_CODE "125")',
                  f'set_tests_properties({word("slow")} PROPERTIES TIMEOUT "1")',
                  f'set_tests_properties({word(MARKED)} PROPERTIES DISABLED "TRUE")']
        (self.tree / "CTestTestfile.cmake").write_text("\n".join(lines) + "\n", encoding="utf-8")
        self.tests, self.here = tests, here
        self.ctest = ctest.CTest("ctest", env, LIMIT)

    def test_listing_decoded_whole(self):
        """Each test's name, command and properties, a non-ASCII name and
        label and a test whose program is not there included."""
        place = {"WORKING_DIRECTORY": self.here}
        expected = {name: ctest.Test(command, dict(place)) for name, command in self.tests.items()}
        expected["gone"] = ctest.Test(None, dict(place))
        expected["pass"].properties.update(LABELS=[ODD, "x"], TIMEOUT=50.0)
        expected["skip"].properties.update(SKIP_RETURN_CODE=125)
        expected["slow"].properties.update(TIMEOUT=1.0)
        expected[MARKED].properties.update(DISABLED=True)
        self.assertEqual(self.ctest.listing(self.tree), expected)

    def test_run_results_and_absent(self):
        """Each result in CTest's words; a name that is no test is absent,
        not a result."""
        junit = self.root / "out" / "run.xml"
        junit.parent.mkdir()
        asked = ["pass", "fail", "skip", "slow", "gone", ODD, MARKED, "unknown"]
        ran = self.ctest.run(self.tree, asked, junit=junit, jobs=2)
        self.assertEqual(ran.status, 8)
        self.assertEqual(ran.absent, ["unknown"])
        self.assertEqual({name: (result.status, result.message) for name, result in ran.results.items()}, {
            "pass": ("run", None), "fail": ("fail", "Failed"), "skip": ("notrun", "SKIP_RETURN_CODE=125"),
            "slow": ("fail", "Timeout"), "gone": ("notrun", "Unable to find executable"), ODD: ("run", None),
            MARKED: ("disabled", None)})
        self.assertTrue(all(isinstance(result.time, float) and result.time >= 0 for result in ran.results.values()))
        self.assertEqual((self.root / "out" / "run.xml.names").read_text(encoding="utf-8").splitlines(), asked)

    def test_run_of_no_test(self):
        """A run whose names are none of the tree's tests says so."""
        with self.assertRaises(ctest.NoTests):
            self.ctest.run(self.tree, ["unknown"], junit=self.root / "none.xml", jobs=1)


if __name__ == "__main__":
    unittest.main()
