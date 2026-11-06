"""K11: each binary of the build's binaries.txt imports only Windows' system
libraries, its API sets among them, or its preset's residual, read by
foundation/devkit/python/imports.py from its bytes; the lists are K11's,
below. The control: qiven_diag_fault_import, which imports the test tree's
own library, is caught.
PE alone is read: K11's ELF half is retired (tools/README.md's Retired
section) and Mach-O was never read, so K11's binaries are skipped off
Windows: a residual."""

import unittest

from foundation.devkit.python import imports
from foundation.devkit.python import system
from foundation.devkit.test.harness import binaries
from foundation.devkit.test.harness.binaries import build_option

PLANTED = "qiven_diag_fault_import"
PLANTED_LIBRARY = "qiven_diag_fault_lib"

# The system's libraries a binary may import, case aside.
WINDOWS_SYSTEM = frozenset(("kernel32.dll", "ntdll.dll", "advapi32.dll", "shell32.dll", "ole32.dll",
                            "bcrypt.dll", "dbghelp.dll"))
# Each preset's residual beyond the list, by its sanitizers.
ASAN_DLL = "clang_rt.asan_dynamic-x86_64.dll"
FUZZ_DLLS = frozenset(("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "ucrtbase.dll", ASAN_DLL))


def outside(read, sanitize):
    """Each import on neither the system's list nor the preset's residual,
    given the build's QIVEN_SANITIZE; an API set, Windows' own by
    system.api_set, passes in every preset."""
    allowed = set(WINDOWS_SYSTEM)
    if "fuzzer" in sanitize:
        allowed |= FUZZ_DLLS
    elif "address" in sanitize:
        allowed.add(ASAN_DLL)
    return [n for n in read.names if n.lower() not in allowed and not system.api_set(n)]


class Imports(unittest.TestCase):
    ctest_labels = ("fake", "fast")

    @unittest.skipUnless(system.WINDOWS, "PE alone is read, so K11 skips off Windows: a residual")
    def test_K11_binaries(self):
        sanitize = build_option("QIVEN_SANITIZE") or ""
        found = {}
        for binary in binaries.listed():
            read = imports.read_file(binary)
            self.assertIn("kernel32.dll", [n.lower() for n in read.names], binary)
            found[binary.stem] = outside(read, sanitize)
        self.assertIn(PLANTED, found)
        # The control: the planted binary's import of the test tree's library, caught.
        self.assertEqual(found.pop(PLANTED), [system.library_file(PLANTED_LIBRARY)])
        self.assertEqual({k: v for k, v in found.items() if v}, {})


if __name__ == "__main__":
    unittest.main()
