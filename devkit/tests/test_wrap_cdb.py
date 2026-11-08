"""cdb found on its pin: QIVEN_CDB unset, or a file off its pin by digest or
by version, is refused with its reason and never read as no cdb, a skip;
the real cdb, which QIVEN_CDB names in this process's environment, is found
on a pin computed here from its file: its digest, and its version from the
file's version resource, a source other than the -version the wrapper
reads. A cdb taken off its pin would debug with tools nobody checked.

The session and the hang's stacks are proven by use, by the C++ part's
debugger cases."""

import ctypes
import hashlib
import os
import sys
import tempfile
import unittest
from ctypes import wintypes
from pathlib import Path

from qiven_devkit.wrap import call, cdb

LIMIT = 60  # seconds for cdb's -version, which answers in under 1 s here


def file_version(path):
    """The file version of its version resource, "a.b.c.d"."""
    version = ctypes.WinDLL("version", use_last_error=True)
    version.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version.GetFileVersionInfoSizeW.argtypes = (wintypes.LPCWSTR, wintypes.LPVOID)
    version.GetFileVersionInfoW.restype = wintypes.BOOL
    version.GetFileVersionInfoW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID)
    version.VerQueryValueW.restype = wintypes.BOOL
    version.VerQueryValueW.argtypes = (wintypes.LPCVOID, wintypes.LPCWSTR, ctypes.POINTER(wintypes.LPVOID),
                                       ctypes.POINTER(wintypes.UINT))
    size = version.GetFileVersionInfoSizeW(path, None)
    if not size:
        raise OSError(ctypes.get_last_error(), f"{path} has no version resource")
    data = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(path, 0, size, data):
        raise OSError(ctypes.get_last_error(), f"{path}: its version resource cannot be read")
    fixed, length = wintypes.LPVOID(), wintypes.UINT()
    if not version.VerQueryValueW(data, "\\", ctypes.byref(fixed), ctypes.byref(length)):
        raise OSError(ctypes.get_last_error(), f"{path}: its version resource holds no fixed part")
    words = ctypes.cast(fixed, ctypes.POINTER(wintypes.DWORD))  # VS_FIXEDFILEINFO: signature, struct, MS, LS
    major, minor = words[2] >> 16, words[2] & 0xFFFF
    build, private = words[3] >> 16, words[3] & 0xFFFF
    return f"{major}.{minor}.{build}.{private}"


def sha256(path):
    with open(path, "rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


@unittest.skipUnless(sys.platform == "win32", "cdb is Windows' debugger")
class OnItsPin(unittest.TestCase):
    def setUp(self):
        self.real = os.environ.get(cdb.VARIABLE)
        self.assertTrue(self.real, f"{cdb.VARIABLE} is not set: it names the cdb these tests check, and its "
                                   "absence is a failure, never a skip")
        self.pin = cdb.Pin(file_version(self.real), sha256(self.real))
        self.base = {"SystemRoot": os.environ["SystemRoot"]}

    def find(self, named, pin):
        env = dict(self.base, **({cdb.VARIABLE: named} if named is not None else {}))
        return cdb.Cdb(env, pin).find(LIMIT)

    def test_unset_is_missing(self):
        for named in (None, ""):
            with self.subTest(named=named), self.assertRaises(call.Missing) as raised:
                self.find(named, self.pin)
            self.assertEqual(raised.exception.call, cdb.FIND)
            self.assertIn(cdb.VARIABLE, raised.exception.reason)
            self.assertIn(self.pin.sha256, raised.exception.reason)

    def test_another_file_is_off_its_pin(self):
        with tempfile.TemporaryDirectory() as scratch:
            other = Path(scratch, "cdb.exe")
            other.write_bytes(b"not cdb")
            with self.assertRaises(cdb.OffPin) as raised:
                self.find(str(other), self.pin)
            reason = raised.exception.reason
            for word in (cdb.VARIABLE, sha256(other), self.pin.sha256):
                self.assertIn(word, reason)

    def test_another_version_is_off_its_pin(self):
        pin = self.pin._replace(version="0.0.0.1")
        with self.assertRaises(cdb.OffPin) as raised:
            self.find(self.real, pin)
        for word in (cdb.VARIABLE, self.pin.version, "0.0.0.1"):
            self.assertIn(word, raised.exception.reason)

    def test_on_its_pin_is_found(self):
        self.assertEqual(self.find(self.real, self.pin), self.real)


if __name__ == "__main__":
    unittest.main()
