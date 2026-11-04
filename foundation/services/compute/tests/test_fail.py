"""H8, a caller's mistake: qiven_compute_fail_demo's one case, an upload into
a buffer another fake gave, run as a process, ends at once through
memory::fail_fast, exit status 0xC0000409 on Windows and SIGABRT elsewhere,
its standard output holding nothing after, and the diagnostic entry's
report on standard error naming the fail-fast first. Not applicable in a
build with QIVEN_HAL off, which builds no compute (H6)."""

import signal
import sys
import unittest

from foundation.devkit.test.runner import binaries
from foundation.devkit.test.runner.binaries import has_hal

DEMO = "qiven_compute_fail_demo"


class CallersMistake(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_H8_ends_the_process(self):
        if not has_hal():
            self.skipTest("not applicable: this build has no compute (QIVEN_HAL off)")
        ran = binaries.run(DEMO, ["--run", "compute.foreign_buffer"])
        if sys.platform == "win32":
            self.assertEqual(ran.status, 0xC0000409)  # STATUS_STACK_BUFFER_OVERRUN, __fastfail's
        else:
            self.assertEqual(ran.status, -signal.SIGABRT)
        self.assertEqual(ran.out.decode("utf-8").splitlines(), ["before"])
        self.assertEqual(ran.err.decode("utf-8").splitlines()[0],
                         "fatal: fail-fast: compute::fake::FakeCompute: a buffer this compute did not give")


if __name__ == "__main__":
    unittest.main()
