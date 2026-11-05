"""K11: each binary of the build's binaries.txt imports only Windows' system
libraries, its API sets among them, or its preset's residual, read by
foundation/devkit/structure/imports.py from its bytes. The control:
qiven_diag_fault_import, which imports the test tree's own library, is
caught.
PE alone is read: K11's ELF half is retired (tools/README.md's Retired
section) and Mach-O was never read, so K11's binaries are skipped off
Windows: a residual."""

import sys
import unittest

from foundation.devkit.test.runner import binaries
from foundation.devkit.test.runner.binaries import build_option
from foundation.devkit.structure import imports

WINDOWS = sys.platform == "win32"
PLANTED = "qiven_diag_fault_import"
PLANTED_LIBRARY = "qiven_diag_fault_lib.dll"


class Imports(unittest.TestCase):
    ctest_labels = ("fake",)

    @unittest.skipUnless(WINDOWS, "PE alone is read, so K11 skips off Windows: a residual")
    def test_K11_binaries(self):
        sanitize = build_option("QIVEN_SANITIZE") or ""
        found = {}
        for binary in binaries.listed():
            read = imports.read_file(binary)
            self.assertIn("kernel32.dll", [n.lower() for n in read.names], binary)
            found[binary.stem] = imports.outside(read, sanitize)
        self.assertIn(PLANTED, found)
        # The control: the planted binary's import of the test tree's library, caught.
        self.assertEqual(found.pop(PLANTED), [PLANTED_LIBRARY])
        self.assertEqual({k: v for k, v in found.items() if v}, {})


if __name__ == "__main__":
    unittest.main()
