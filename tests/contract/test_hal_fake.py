"""The HAL's cases on its fake, every system of the CI matrix; not
applicable in a build with QIVEN3_HAL off, which has no HAL (H6)."""

import shutil
import tempfile
import unittest
from pathlib import Path

from tests.contract.hal_cases import HalCases
from tests.drive.session import Drive, has_hal


class Fake(HalCases, unittest.TestCase):
    ctest_labels = ("fake",)

    def setUp(self):
        if not has_hal():
            self.skipTest("not applicable: this build has no HAL (QIVEN3_HAL off)")
        self.root = Path(tempfile.mkdtemp(prefix="qiven3-h-"))
        self.addCleanup(shutil.rmtree, self.root, True)

    def drive(self):
        return Drive("--platform", "fake", "--root", self.root, "--hal", "fake")
