"""The platform's cases on the fake, every system of the CI matrix, through
qiven3-drive-fake.

The fake has files, and lacks processes and machine, so P20 runs nothing
on it; its own programs, by set_programs, and its lock are platform's own
(foundation/system/platform/tests/fake_process_test.cpp and
files_test.cpp). Its files' operations write under its root alone.
"""

import unittest

from tests.contract.platform_cases import PlatformBinding
from tests.contract.platform_files import PlatformFiles
from tests.contract.platform_location import PlatformLocation
from tests.contract.platform_machine import CASES, PlatformForms, PlatformMachine, skip_reasons
from tests.contract.platform_memory import PlatformMemory
from tests.contract.platform_process import CASES as PROCESS_CASES
from tests.contract.platform_process import PlatformProcess
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

    def files_root(self):
        return self.root()

    def hold(self, d, **times):
        """The fake's holders: its busy counts, none given none."""
        d.call("set_busy", **times)


class Fake(FakeBinding, PlatformLocation, PlatformFiles, PlatformProcess, PlatformMemory, unittest.TestCase):
    ctest_labels = ("fake",)


class FakeMachine(FakeBinding, PlatformMachine, PlatformForms, unittest.TestCase):
    """The machine cases, not applicable on the fake: label machine, not fake."""
    ctest_labels = ("machine",)


class FakeMachineSkips(unittest.TestCase):
    """FakeMachine's cases P17, P18, P21 and P22 run here, under fake: each
    not applicable, by its reason, reaching no member."""
    ctest_labels = ("fake",)

    def test_machine_not_applicable(self):
        reasons, others = skip_reasons(FakeMachine)
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(CASES, "not applicable: machine: the fake adapter lacks it"))


class FakeProcessSkips(unittest.TestCase):
    """Fake's case P20 runs here on a binding whose drive and
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
