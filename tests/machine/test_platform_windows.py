"""The platform's cases on the Windows adapter, here, through
qiven3-drive-windows: as on the fake, the files' failed open under faults
and the memory cases, each operation's budget and its faults; P20's child
half and the process readings, over qiven_platform_child's children; and
the machine cases P17, P18 and P21. Those that run qiven3, location's and
P22, are in machine/test_platform_location.py.

Each case's drive holds as many handles at its end as before it;
HandleCount plants a leak.
"""

import sys
import unittest

from tests.contract.platform_cases import PlatformBinding
from tests.contract.platform_files import PlatformFiles
from tests.contract.platform_machine import PlatformMachine, skip_reasons
from tests.contract.platform_memory import PlatformMemory
from tests.contract.platform_process import PlatformProcess
from tests.drive.session import WINDOWS, Drive


class WindowsBinding(PlatformBinding):
    ADAPTER = "windows"
    DRIVE = WINDOWS
    # Its cases' operations: create, read, remove_directory, replace and
    # stage are win/files'; facts, synced and residual win/facts';
    # location's commands run as the binary, qiven3 location ..., the
    # drive answering store_dir, the facts and the clock the system's;
    # program, start and run are win/process', their children
    # qiven_platform_child's; program is the machine cases' too.
    SYSTEM_LOCATION = True
    PROCESSES = True
    FACTS = True
    PROGRAM = True
    RESIDUAL = True
    TAGS = frozenset({"files", "processes", "machine"})
    COUNT_HANDLES = True
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")
        super().setUp()

    def drive_args(self, config=None):
        args = []
        if config is not None:
            args += ["--config", config]
        return args

    def files_root(self):
        return self.scratch


class Windows(WindowsBinding, PlatformFiles, PlatformProcess, PlatformMemory, PlatformMachine, unittest.TestCase):
    pass


# The files' and processes' cases' skips: none. P20, the last of them to
# skip, runs whole, its two init runs over qiven3-child init; its lock
# half and P9 are platform's own (win_files_held_test.cpp).
FILES_UNTIL = {}
# The cases pinned running whole here, no skip: P20.
FILES_WHOLE = ("test_P20_lock",)


class FilesUntil(unittest.TestCase):
    """Windows' files and processes cases run here, none skipping: P20
    whole, two init children at once."""
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")

    def test_files_until(self):
        reasons, others = skip_reasons(Windows, FILES_WHOLE + tuple(FILES_UNTIL))
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(FILES_WHOLE) | FILES_UNTIL)


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
