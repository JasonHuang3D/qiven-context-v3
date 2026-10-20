"""The platform's cases on the Windows adapter, here: those needing
neither processes nor machine, as on the fake, and the machine cases.

Its configuration location with no override is LocalAppData's qiven3, the
known folder read here through the system's own call.
"""

import sys
import unittest

from tests.contract.platform_cases import PlatformBinding, PlatformCases
from tests.contract.platform_rest import PlatformRest, PlatformRestMachine


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
    TAGS = frozenset({"files", "processes", "machine"})
    ctest_labels = ("drive",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows adapter is built on Windows alone")
        super().setUp()

    def drive_args(self, config=None, seed=None):
        args = ["--platform", "windows"]
        if config is not None:
            args += ["--config", config]
        return args

    def default_config(self):
        return local_app_data() + "\qiven3"


class Windows(WindowsBinding, PlatformCases, PlatformRest, PlatformRestMachine, unittest.TestCase):
    pass
