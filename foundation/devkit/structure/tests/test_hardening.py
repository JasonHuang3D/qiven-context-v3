"""K19: what the build's hardening leaves in each Linux binary of the build's
binaries.txt, read by foundation/devkit/structure/hardening.py from its bytes, beside
K11's imports, in the same jobs:
- PIE: ET_DYN with DF_1_PIE;
- relro and now: PT_GNU_RELRO, with DF_BIND_NOW or DF_1_NOW;
- the build id: an NT_GNU_BUILD_ID note;
- the stack protector: the import of __stack_chk_fail, by every binary,
  each holding a protected frame;
- CET: the GNU property note's X86_FEATURE_1_AND with IBT and SHSTK,
  where it survives: the presets by gcc. A residual by clang under a
  sanitizer (linux-clang, linux-tsan, linux-fuzz, linux-msan): clang's
  sanitizer runtimes, libclang_rt.asan, asan_static, asan_cxx, tsan,
  tsan_cxx, msan and msan_cxx, and the prefix's MemorySanitizer libc++.a,
  carry no property, so the linker drops it; there it is not checked;
- fortify: glibc's __*_chk imports, by at least one binary where the build
  optimizes and no sanitizer runs (linux-plain): fortify acts only where a
  size is known at compile time, as in SQLite's copies; none required
  elsewhere.
Stack-clash probes leave nothing in the headers, notes or symbols: they
are instructions, emitted only in a frame past the probe interval, a page,
or of a dynamic size, which the 4,096-byte frame bound leaves few of, and
the reader reads no instructions: a residual. On macOS (Mach-O) and on
Windows nothing is read.
"""

import sys
import unittest

from tests.drive.session import build_option
from foundation.devkit.structure import hardening
from foundation.devkit.structure.tests.test_binaries import listed

LINUX = sys.platform.startswith("linux")
CLANG = "Clang"
OPTIMIZED = ("Release", "RelWithDebInfo", "MinSizeRel")
PRODUCT = "qiven3"


def preset():
    """What the build's preset holds: (cet expected, fortify expected)."""
    sanitize = build_option("QIVEN_SANITIZE") or ""
    compiler = build_option("QIVEN_CXX_COMPILER_ID") or ""
    optimized = (build_option("CMAKE_BUILD_TYPE") or "") in OPTIMIZED
    return not (sanitize and compiler == CLANG), optimized and not sanitize


@unittest.skipUnless(LINUX, "ELF's hardening is Linux's: Mach-O is not read, Windows' binaries are PE")
class Hardening(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_K19_binaries(self):
        cet, fortify = preset()
        lacks, fortified = {}, []
        for binary in listed():
            read = hardening.read_file(binary)
            lacks[binary.stem] = hardening.missing(read, cet)
            if any(hardening.checked(n) for n in read.undefined):
                fortified.append(binary.stem)
        self.assertIn(PRODUCT, lacks)
        self.assertEqual({k: v for k, v in lacks.items() if v}, {})
        if fortify:
            self.assertNotEqual(fortified, [], "no binary imports a checking function under fortify")


if __name__ == "__main__":
    unittest.main()
