"""K11: each binary of the build's binaries.txt imports only its system's
libraries, Windows' API sets among Windows' own, or its preset's residual,
read by foundation/devkit/structure/imports.py
from its bytes; on Linux it names its loader, holds no run path, and needs
no glibc symbol version past the floor's, GLIBC_2.39. The control:
qiven3-fault-import, which imports the test tree's own library, is caught.
On macOS Mach-O is not read, so K11's binaries are skipped there: a
residual."""

import sys
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.session import build_option
from foundation.devkit.structure import imports

WINDOWS = sys.platform == "win32"
SYSTEM = "windows" if WINDOWS else "linux"
PLANTED = "qiven3-fault-import"
PLANTED_LIBRARY = "qiven3_fault_lib.dll" if WINDOWS else "libqiven3_fault_lib.so"
LOADER = "/lib64/ld-linux-x86-64.so.2"
MACOS = sys.platform == "darwin"


def listed():
    build = binaries.directory("QIVEN3_BUILD")
    text = (build / "binaries.txt").read_text(encoding="utf-8")
    return [Path(line).resolve() for line in text.splitlines() if line.strip()]


class Imports(unittest.TestCase):
    ctest_labels = ("fake",)

    @unittest.skipIf(MACOS, "Mach-O is not read, so K11 skips macOS: a residual")
    def test_K11_binaries(self):
        sanitize = build_option("QIVEN_SANITIZE") or ""
        compiler = build_option("QIVEN_CXX_COMPILER_ID") or ""
        found, past = {}, {}
        for binary in listed():
            read = imports.read_file(binary)
            self.assertEqual(read.kind, "pe" if WINDOWS else "elf", binary)
            if not WINDOWS:
                self.assertEqual((read.interp, read.run_paths), (LOADER, ()), binary)
                self.assertTrue(imports.newest_glibc(read), binary)
                past[binary.stem] = imports.past_floor(read)
            self.assertIn("kernel32.dll" if WINDOWS else "libc.so.6", [n.lower() for n in read.names], binary)
            found[binary.stem] = imports.outside(read, SYSTEM, sanitize, compiler)
        self.assertIn(PLANTED, found)
        # The control: the planted binary's import of the test tree's library, caught.
        self.assertEqual(found.pop(PLANTED), [PLANTED_LIBRARY])
        self.assertEqual({k: v for k, v in found.items() if v}, {})
        self.assertEqual({k: v for k, v in past.items() if v}, {})


if __name__ == "__main__":
    unittest.main()
