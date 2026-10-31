"""The platform's cases that run qiven3, on the Windows adapter's binding
(machine/test_platform_windows.py): in the binary's module, which selects them."""

import sys
import unittest

from tests.contract.platform_location import PlatformLocation
from tests.contract.platform_machine import PlatformForms, skip_reasons
from tests.machine.test_platform_windows import WindowsBinding


class WindowsLocation(WindowsBinding, PlatformLocation, PlatformForms, unittest.TestCase):
    pass


# The machine cases' skips until their members land: none. P17, P18 and
# P21 run; P22, the last of them to skip, runs through location's init
# as the binary, its synced forms each skipping alone by its reason
# where this machine has no OneDrive folder; P6, the fake's warnings, is
# not applicable here.
UNTIL = {"test_P6_warnings": "not applicable: the warnings are the fake's facts, set by request"}
# The cases pinned running here, no skip: P22.
MACHINE_RUNS = ("test_P22_forms_here",)


class MachineUntil(unittest.TestCase):
    """Windows' case P22 runs here, no skip at its own level, through
    location's init as the binary; P6 is not applicable, by its reason."""
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")

    def test_machine_until(self):
        reasons, others = skip_reasons(WindowsLocation, MACHINE_RUNS + tuple(UNTIL))
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(MACHINE_RUNS) | UNTIL)
