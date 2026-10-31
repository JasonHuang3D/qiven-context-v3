"""The HAL's cases on its fake, every system of the CI matrix; not
applicable in a build with QIVEN_HAL off, which has no HAL (H6)."""

import shutil
import tempfile
import unittest
from pathlib import Path

from tests.contract.hal_cases import HalCases
from tests.contract.hal_memory import HalMemory
from tests.drive.session import Drive, has_hal, once


class Fake(HalCases, HalMemory, unittest.TestCase):
    ctest_labels = ("fake",)

    def setUp(self):
        if not has_hal():
            self.skipTest("not applicable: this build has no HAL (QIVEN_HAL off)")
        self.root = Path(tempfile.mkdtemp(prefix="qiven3-h-"))
        self.addCleanup(shutil.rmtree, self.root, True)

    def drive(self):
        return Drive("--root", self.root, "--hal", "fake")

    def drive_once(self, *requests):
        return once(["--root", self.root, "--hal", "fake"], *requests)
