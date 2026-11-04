"""The drive's HAL members on the HAL's fake, every system of the CI
matrix; not applicable in a build with QIVEN_HAL off, which has no HAL
(H6). Compute's own cases are its own (foundation/services/compute/tests/)."""

import shutil
import tempfile
import unittest
from pathlib import Path

from tests.contract.hal_memory import HalMemory
from foundation.devkit.test.runner.binaries import has_hal
from tests.drive.session import Drive


class Fake(HalMemory, unittest.TestCase):
    ctest_labels = ("fake",)

    def setUp(self):
        if not has_hal():
            self.skipTest("not applicable: this build has no HAL (QIVEN_HAL off)")
        self.root = Path(tempfile.mkdtemp(prefix="qiven3-h-"))
        self.addCleanup(shutil.rmtree, self.root, True)

    def drive(self):
        return Drive("--root", self.root, "--hal", "fake")
