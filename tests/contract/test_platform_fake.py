"""The platform's cases on the fake, every system of the CI matrix, through
qiven3-drive-fake.

The fake has files, and lacks processes and machine. Its configuration
location with no override is the drive's, "<root>/qiven3"; its files'
operations write under its root alone.
"""

import unittest

from tests.contract.platform_cases import PlatformBinding, PlatformCases
from tests.contract.platform_last import PlatformLast
from tests.contract.platform_rest import PlatformRest, PlatformRestMachine
from tests.drive.session import FAKE


class FakeBinding(PlatformBinding):
    ADAPTER = "fake"
    DRIVE = FAKE
    TAGS = frozenset({"files"})

    def root(self):
        root = self.scratch / "root"
        root.mkdir(exist_ok=True)
        return root

    def drive_args(self, config=None, seed=None):
        args = ["--root", str(self.root())]
        if config is not None:
            args += ["--config", config]
        if seed is not None:
            args += ["--seed", seed]
        return args

    def default_config(self):
        return f"{self.root()}/qiven3"

    def files_root(self):
        return self.root()


class Fake(FakeBinding, PlatformCases, PlatformRest, PlatformLast, unittest.TestCase):
    ctest_labels = ("fake",)


class FakeMachine(FakeBinding, PlatformRestMachine, unittest.TestCase):
    """The machine cases, not applicable on the fake: no label fake."""
