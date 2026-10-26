"""K20: the standard library's hardened mode, run. qiven3-fault span reads
index 1 of a span over the first of two elements; each library stops the
process before the read, in every build, sanitizers' included:
- libc++ (linux-msan, against the prefix's libc++ under MemorySanitizer):
  the extensive mode's trap, SIGILL. The case asserts that the trap fires,
  not the library's words: whether a release writes a line before it, or
  leaves a message in the binary, is the toolchain's;
- libstdc++ (_GLIBCXX_ASSERTIONS, every other Linux build): its assertion's
  line, then SIGABRT;
- MSVC's library (_MSVC_STL_HARDENING): a fail-fast with no line written,
  0xC0000409. On the debug runtime its assertion reaches the report hook
  first: not observed here, so skipped there.
The read did not happen: the plant writes a line before the read and one
after it only if it read, which is absent. The control, span-in, the same
read in bounds, writes the line after and exits 0.
"""

import signal
import sys
import unittest

from tests.drive import binaries
from tests.drive.session import build_option
from tests.drive.test_faults import FAULT, run_watched
from tools import status as status_table

WINDOWS = sys.platform == "win32"
BEFORE = "span: reading"
AFTER = "span: read "  # the line after the read: "span: read past the end" or "span: read in bounds"
FAIL_FAST = 0xC0000409
LIBSTDCXX_LINE = r".*[/\\]span:\d+: .*Assertion '__idx < size\(\)' failed\."


def library():
    """The build's standard library: "msvc", "libc++" or "libstdc++"."""
    if WINDOWS:
        return "msvc"
    return "libc++" if "-stdlib=libc++" in (build_option("CMAKE_EXE_LINKER_FLAGS") or "") else "libstdc++"


class Hardened(unittest.TestCase):
    ctest_labels = ("fake",)

    def run_span(self, plant):
        if WINDOWS and build_option("CMAKE_BUILD_TYPE") == "Debug":
            self.skipTest("the debug runtime's assertion reaches its report hook first: not observed here")
        ran, out, err, seconds, seen = run_watched([binaries.path(FAULT), plant], "K20")
        text = err.decode("utf-8", "replace")
        self.assertEqual(seen, [], f"K20: a window opened: {text}")
        return ran, text.splitlines(), text

    def test_K20_span(self):
        ran, lines, text = self.run_span("span")
        found = library()
        status = {"msvc": FAIL_FAST, "libc++": 128 + signal.SIGILL, "libstdc++": 128 + signal.SIGABRT}[found]
        self.assertEqual(ran, status, f"K20 {found}: {text}")
        self.assertEqual(lines[0] if lines else "", BEFORE, text)
        self.assertEqual([line for line in lines if line.startswith(AFTER)], [], f"K20: the read happened: {text}")
        self.assertEqual(lines[-1] if lines else "", status_table.line(status if WINDOWS else 128 - status), text)
        if found == "libstdc++":
            self.assertRegex(lines[1] if len(lines) > 1 else "", f"^{LIBSTDCXX_LINE}$", text)
        print(f"K20: {found} stopped the read past the span's end: {lines[-1]}")

    def test_K20_control(self):
        """span-in, the read in bounds: the line after the read, exit 0."""
        ran, lines, text = self.run_span("span-in")
        self.assertEqual(ran, 0, text)
        self.assertEqual(lines[:2], [BEFORE, "span: read in bounds"], text)
        self.assertTrue(lines[1].startswith(AFTER), text)


if __name__ == "__main__":
    unittest.main()
