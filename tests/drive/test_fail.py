"""fail_fast ends the process at once: qiven3_fail_demo's one case, run as a
process, exits with std::terminate's status and writes nothing after."""

import sys
import unittest

from tests.drive import binaries

DEMO = "qiven3_fail_demo"


class FailFast(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_ends_the_process(self):
        ran = binaries.run(DEMO, ["--run", "fail.fail_fast"])
        if sys.platform == "win32":
            # abort() under the runner's settings: no message, no dialog.
            self.assertEqual(ran.status, 3)
        else:
            self.assertLess(ran.status, 0)
        self.assertEqual(ran.out.decode("utf-8").splitlines(), ["before"])
        self.assertEqual(ran.err, b"")


if __name__ == "__main__":
    unittest.main()
