"""The platform's cases on the Windows adapter, here, through
qiven3-drive-windows: those needing neither processes nor machine, as on
the fake, the process cases over qiven3-child platform's children, and the
machine cases; the fake's own cases, not applicable here.

Its configuration location with no override is LocalAppData's qiven3, the
known folder read here through the system's own call. Each case's drive
holds as many handles at its end as before it; HandleCount plants a leak.
The files' holders are real handles (machine/windows_holds.py).
"""

import sys
import unittest

from tests.contract.platform_cases import PlatformBinding, PlatformCases
from tests.contract.platform_files import PlatformFiles
from tests.contract.platform_last import PlatformLast
from tests.contract.platform_location import PlatformLocation
from tests.contract.platform_machine import PlatformMachine, skip_reasons
from tests.contract.platform_memory import PlatformMemory
from tests.contract.platform_own import CASES as OWN_CASES
from tests.contract.platform_own import PlatformOwn
from tests.contract.platform_process import PlatformProcess
from tests.contract.platform_rest import PlatformRest, PlatformRestMachine
from tests.drive.session import WINDOWS, Drive
from tests.machine.windows_holds import HeldDrive


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
    # create, read, remove_directory, replace, lock and stage are
    # win/files'; facts, synced and residual win/facts'; location's
    # commands run as the binary, qiven3 location ..., the drive answering
    # store_dir, the facts and the clock the system's; program, start and
    # run are win/process', their children qiven3-child platform's;
    # program is the machine cases'.
    CREATE_READ = True
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

    def drive_args(self, config=None, seed=None):
        args = []
        if config is not None:
            args += ["--config", config]
        return args

    def default_config(self):
        return local_app_data() + "\\qiven3"

    def files_root(self):
        return self.scratch

    def drive(self, config=None, seed=None, env=None, cwd=None):
        return HeldDrive(*self.drive_args(config, seed), drive=self.DRIVE, env=env, cwd=cwd,
                         count_handles=self.COUNT_HANDLES)

    def hold(self, d, **times):
        """The holders: real handles, armed on the next request naming a
        path (machine/windows_holds.py)."""
        d.hold(**times)

    def held(self, d, kind):
        """The kind's last holder's own times, (taken, released)."""
        return d.holder_times(kind)


class Windows(WindowsBinding, PlatformCases, PlatformLocation, PlatformFiles, PlatformProcess, PlatformOwn,
              PlatformRest, PlatformLast, PlatformMemory, PlatformMachine, PlatformRestMachine, unittest.TestCase):
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
        reasons, others = skip_reasons(Windows, MACHINE_RUNS + tuple(UNTIL))
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(MACHINE_RUNS) | UNTIL)


# The files' and processes' cases' skips: none. P20, the last of them to
# skip, runs whole, its child half's two init runs over qiven3-child
# platform init, after its lock half, timed; P10 to P15, P19 and P25 run
# whole.
FILES_UNTIL = {}
# The cases pinned running whole here, no skip: P20, its child half last.
FILES_WHOLE = ("test_P20_lock",)


class FilesUntil(unittest.TestCase):
    """Windows' files and processes cases run here, none skipping: P20
    whole, its lock half timed on a lock held by another handle, its child
    half two init children at once; P19 and P25 run through stage, P10 to
    P15 over qiven3-child."""
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")

    def test_files_until(self):
        reasons, others = skip_reasons(Windows, FILES_WHOLE + tuple(FILES_UNTIL))
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(FILES_WHOLE) | FILES_UNTIL)


class OwnNotApplicable(unittest.TestCase):
    """The fake's own cases run here on the Windows adapter: each not
    applicable, by its reason."""
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")

    def test_own_not_applicable(self):
        reasons, others = skip_reasons(Windows, OWN_CASES)
        self.assertEqual(others, [])
        self.assertEqual(reasons, dict.fromkeys(OWN_CASES, "not applicable: own: fake: the windows adapter lacks it"))


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
