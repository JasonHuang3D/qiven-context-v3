"""The platform's cases on the Windows adapter, here, through
qiven3-drive-windows: those needing neither processes nor machine, as on
the fake, and the machine cases.

Its configuration location with no override is LocalAppData's qiven3, the
known folder read here through the system's own call. Each case's drive
holds as many handles at its end as before it; HandleCount plants a leak.
"""

import sys
import unittest

from tests.contract.platform_cases import PlatformBinding, PlatformCases
from tests.contract.platform_files import PlatformFiles
from tests.contract.platform_last import PlatformLast
from tests.contract.platform_location import PlatformLocation
from tests.contract.platform_machine import PlatformMachine, skip_reasons
from tests.contract.platform_memory import PlatformMemory
from tests.contract.platform_process import PlatformProcess
from tests.contract.platform_rest import PlatformRest, PlatformRestMachine
from tests.drive.session import WINDOWS, Drive


def local_app_data():
    """The known folder LocalAppData, by SHGetKnownFolderPath."""
    import ctypes
    import uuid

    folder_id = (ctypes.c_ubyte * 16).from_buffer_copy(
        uuid.UUID("f1b32785-6fba-4fcf-9d55-7b8e7f157091").bytes_le)
    found = ctypes.c_wchar_p()
    shell32 = ctypes.WinDLL("shell32")
    status = shell32.SHGetKnownFolderPath(ctypes.byref(folder_id), 0, None, ctypes.byref(found))
    try:
        if status != 0 or found.value is None:
            raise OSError(f"SHGetKnownFolderPath: HRESULT {status & 0xFFFFFFFF:#010x}")
        return found.value
    finally:
        ctypes.WinDLL("ole32").CoTaskMemFree(found)


class WindowsBinding(PlatformBinding):
    ADAPTER = "windows"
    DRIVE = WINDOWS
    # create, read, remove_directory, replace and lock are win/files'; its
    # stage, and the drive's holders over real handles, are left of it in
    # package 5; the drive's location request is the fake's alone until
    # then; program, start and run are win/process', the children
    # qiven3-child's, in package 5 too; so are facts and synced
    # (win/facts'), program (win/process') and residual, the machine cases'.
    CREATE_READ = True
    STAGE_LOCK = False
    LOCATION = False
    PROCESSES = False
    FACTS = False
    PROGRAM = False
    RESIDUAL = False
    TAGS = frozenset({"files", "processes", "machine"})
    COUNT_HANDLES = True
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")
        super().setUp()

    def drive_args(self, config=None, seed=None):
        args = []
        if config is not None:
            args += ["--config", config]
        return args

    def default_config(self):
        return local_app_data() + "\\qiven3"

    def files_root(self):
        return self.scratch


class Windows(WindowsBinding, PlatformCases, PlatformLocation, PlatformFiles, PlatformProcess, PlatformRest,
              PlatformLast, PlatformMemory, PlatformMachine, PlatformRestMachine, unittest.TestCase):
    pass


# The machine cases' skips until their members land; each case's flag
# set true drops its entry here.
UNTIL = {
    "test_P17_facts_here": "until package 5: facts: win/facts",
    "test_P18_residual_here": "until package 5: residual",
    "test_P21_program": "until package 5: program: win/process",
    "test_P22_forms_here": "until package 5: synced: win/facts",
}


class MachineUntil(unittest.TestCase):
    """Windows' cases P17, P18, P21 and P22 run here: each member answers
    unsupported, and the case skips, by its reason."""
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")

    def test_machine_until(self):
        reasons, others = skip_reasons(Windows, tuple(UNTIL))
        self.assertEqual(others, [])
        self.assertEqual(reasons, UNTIL)


def plant_handle(pid):
    """A handle of this process duplicated into process pid, which then
    holds one handle more."""
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.GetCurrentProcess.restype = wintypes.HANDLE
    kernel32.DuplicateHandle.argtypes = (wintypes.HANDLE, wintypes.HANDLE, wintypes.HANDLE,
                                         ctypes.POINTER(wintypes.HANDLE), wintypes.DWORD,
                                         wintypes.BOOL, wintypes.DWORD)
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    target = kernel32.OpenProcess(0x0040, False, pid)  # PROCESS_DUP_HANDLE
    if not target:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        out = wintypes.HANDLE()
        own = kernel32.GetCurrentProcess()
        if not kernel32.DuplicateHandle(own, own, target, ctypes.byref(out), 0, False, 2):  # SAME_ACCESS
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel32.CloseHandle(target)


class HandleCount(unittest.TestCase):
    """The handle count's plant: a handle put into the drive between its
    two counts fails the test's end, and the control, none put, passes."""
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")

    def test_planted_leak(self):
        with Drive(drive=WINDOWS, count_handles=True) as control:
            control.call("now")
        drive = Drive(drive=WINDOWS, count_handles=True)
        drive.call("now")
        plant_handle(drive.process.pid)
        with self.assertRaises(AssertionError) as caught:
            drive.close()
        self.assertIn("handles at the test's end", str(caught.exception))
