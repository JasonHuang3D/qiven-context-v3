"""The platform's cases on the fake, every system of the CI matrix.

The fake has files, and lacks processes and machine. Its configuration
location with no override is the drive's, "<root>/qiven3".
"""

import unittest

from tests.contract.platform_cases import PlatformBinding, PlatformCases
from tests.contract.platform_rest import PlatformRest, PlatformRestMachine


class FakeBinding(PlatformBinding):
    ADAPTER = "fake"
    TAGS = frozenset({"files"})

    def root(self):
        root = self.scratch / "root"
        root.mkdir(exist_ok=True)
        return root

    def drive_args(self, config=None, seed=None):
        args = ["--platform", "fake", "--root", str(self.root())]
        if config is not None:
            args += ["--config", config]
        if seed is not None:
            args += ["--seed", seed]
        return args

    def default_config(self):
        return f"{self.root()}/qiven3"


class Fake(FakeBinding, PlatformCases, PlatformRest, unittest.TestCase):
    ctest_labels = ("fake",)


class FakeMachine(FakeBinding, PlatformRestMachine, unittest.TestCase):
    """The machine cases, not applicable on the fake: no label fake."""
