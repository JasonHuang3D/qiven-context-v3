"""One test per route of the Python side, for test_runner.py to run as a
process with -p samples.py. Its name does not match test*.py, so no
other run discovers it: one of these fails and one skips by design.
"""

import unittest


class Samples(unittest.TestCase):
    def test_passes(self):
        self.assertTrue(True)

    def test_fails(self):
        self.fail("the sample's failure")

    def test_skipped(self):
        self.skipTest("not applicable: the sample's skip")


@unittest.skip("not applicable: the sample class's skip")
class SkippedClass(unittest.TestCase):
    def test_any(self):
        self.fail("not run")
