"""fail_fast ends the process at once: qiven3_fail_demo's one case, run as a
process, exits with the fail-fast's status, 0xC0000409 on Windows and
SIGABRT elsewhere, its standard output holding nothing after, and the
diagnostic entry's report on standard error naming the kept cause first."""

import signal
import sys
import unittest

from tests.drive import binaries

DEMO = "qiven3_fail_demo"


class FailFast(unittest.TestCase):
    ctest_reads = ("qiven3_fail_demo",)
    ctest_labels = ("fake",)

    def test_ends_the_process(self):
        ran = binaries.run(DEMO, ["--run", "fail.fail_fast"])
        if sys.platform == "win32":
            self.assertEqual(ran.status, 0xC0000409)  # STATUS_STACK_BUFFER_OVERRUN, __fastfail's
        else:
            self.assertEqual(ran.status, -signal.SIGABRT)
        self.assertEqual(ran.out.decode("utf-8").splitlines(), ["before"])
        self.assertEqual(ran.err.decode("utf-8").splitlines()[0],
                         "fatal: fail-fast: fail.fail_fast: the case's own invariant")


if __name__ == "__main__":
    unittest.main()
