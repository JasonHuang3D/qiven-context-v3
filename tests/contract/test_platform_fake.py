"""The platform's cases on the fake, every system of the CI matrix, through
qiven3-drive-fake.

The fake has files and its own cases ("own: fake", contract/platform_own.py:
its programs, by set_programs, and its lock), and lacks processes and
machine, so P10 to P15 and P20 run nothing on it. Its configuration
location with no override is the drive's, "<root>/qiven3"; its files'
operations write under its root alone.
"""

import unittest

from tests.contract.platform_cases import PlatformBinding, PlatformCases
from tests.contract.platform_files import PlatformFiles
from tests.contract.platform_last import PlatformLast
from tests.contract.platform_location import PlatformLocation
from tests.contract.platform_machine import CASES, PlatformForms, PlatformMachine, skip_reasons
from tests.contract.platform_memory import PlatformMemory
from tests.contract.platform_own import PlatformOwn
from tests.contract.platform_process import CASES as PROCESS_CASES
from tests.contract.platform_process import PlatformProcess
from tests.contract.platform_rest import PlatformRest, PlatformRestMachine
from tests.drive.session import FAKE


class FakeBinding(PlatformBinding):
    ADAPTER = "fake"
    DRIVE = FAKE
    TAGS = frozenset({"files", "own: fake"})

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


class Fake(FakeBinding, PlatformCases, PlatformLocation, PlatformFiles, PlatformProcess, PlatformOwn,
           PlatformRest, PlatformLast, PlatformMemory, unittest.TestCase):
    ctest_labels = ("fake",)


class FakeMachine(FakeBinding, PlatformMachine, PlatformForms, PlatformRestMachine, unittest.TestCase):
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


class FakeProcessSkips(unittest.TestCase):
    """Fake's cases P10 to P15 and P20 run here on a binding whose drive and
    files' root fail the case if reached: each not applicable, by its
    reason, having run nothing."""
    ctest_labels = ("fake",)

    def test_processes_not_applicable(self):
        class Untouched(Fake):
            def drive(self, *args, **kwargs):
                raise AssertionError("a drive started before the need was checked")

            def files_root(self):
                raise AssertionError("the files' root reached before the need was checked")

        reasons, others = skip_reasons(Untouched, PROCESS_CASES)
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(PROCESS_CASES, "not applicable: processes: the fake adapter lacks it"))
