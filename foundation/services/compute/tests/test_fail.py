"""H8, a caller's mistake: qiven_compute_fail_demo's one case, an upload into
a buffer another fake gave, run as a process, ends at once through
memory::fail_fast, exit status system's fail_fast_status, its standard output holding nothing after, and the diagnostic entry's
report on standard error naming the fail-fast first."""

import unittest

from foundation.devkit.python import system
from foundation.devkit.test.harness import binaries

DEMO = "qiven_compute_fail_demo"


class CallersMistake(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_H8_ends_the_process(self):
        ran = binaries.run(DEMO, ["--run", "compute.foreign_buffer"])
        self.assertEqual(ran.status, system.fail_fast_status())
        self.assertEqual(ran.out.decode("utf-8").splitlines(), ["before"])
        self.assertEqual(ran.err.decode("utf-8").splitlines()[0],
                         "fatal: fail-fast: compute::fake::FakeCompute: a buffer this compute did not give")


if __name__ == "__main__":
    unittest.main()
