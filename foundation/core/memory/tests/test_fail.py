"""fail_fast ends the process at once: qiven_memory_fail_demo's one case, run as a
process, exits with the fail-fast's status, system's fail_fast_status, its
standard output holding nothing after, and the
diagnostic entry's report on standard error naming the kept cause first."""

import unittest

from foundation.devkit.python import system
from foundation.devkit.test.runner import binaries

DEMO = "qiven_memory_fail_demo"


class FailFast(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_ends_the_process(self):
        ran = binaries.run(DEMO, ["--run", "fail.fail_fast"])
        self.assertEqual(ran.status, system.fail_fast_status())
        self.assertEqual(ran.out.decode("utf-8").splitlines(), ["before"])
        self.assertEqual(ran.err.decode("utf-8").splitlines()[0],
                         "fatal: fail-fast: fail.fail_fast: the case's own invariant")


if __name__ == "__main__":
    unittest.main()
