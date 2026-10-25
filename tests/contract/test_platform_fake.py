"""The platform's cases on the fake, every system of the CI matrix, through
qiven3-drive-fake.

The fake has files, and lacks processes and machine: its programs, by
set_programs, carry the processes' cases' fake halves. Its configuration
location with no override is the drive's, "<root>/qiven3"; its files'
operations write under its root alone.
"""

import unittest

from tests.contract.platform_cases import PlatformBinding, PlatformCases
from tests.contract.platform_files import PlatformFiles
from tests.contract.platform_last import PlatformLast
from tests.contract.platform_location import PlatformLocation
from tests.contract.platform_machine import CASES, PlatformMachine, skip_reasons
from tests.contract.platform_memory import PlatformMemory
from tests.contract.platform_process import PlatformProcess
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

    def hold(self, d, **times):
        """The fake's holders: its busy counts, none given none."""
        d.call("set_busy", **times)


class Fake(FakeBinding, PlatformCases, PlatformLocation, PlatformFiles, PlatformProcess, PlatformRest,
           PlatformLast, PlatformMemory, unittest.TestCase):
    ctest_labels = ("fake",)


class FakeMachine(FakeBinding, PlatformMachine, PlatformRestMachine, unittest.TestCase):
    """The machine cases, not applicable on the fake: label machine, not fake."""
    ctest_labels = ("machine",)


class FakeMachineSkips(unittest.TestCase):
    """FakeMachine's cases P17, P18, P21 and P22 run here, in tier 1: each
    not applicable, by its reason, reaching no member."""
    ctest_labels = ("fake",)

    def test_machine_not_applicable(self):
        reasons, others = skip_reasons(FakeMachine)
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(CASES, "not applicable: machine: the fake adapter lacks it"))
