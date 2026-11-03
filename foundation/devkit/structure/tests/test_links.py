"""964: each library on a link line once. Apple's linker warns that it
ignores a library named twice on one link line, and the macOS build's links
make their warnings errors, so such a line fails there. The build's own
link lines, read from its build.ninja by foundation/devkit/structure/links.py, hold no
library twice, on every system the build runs on: no job builds macOS
now, and Windows' and Linux's builds stand for it. Every link
the build has is read; the five that failed on macOS must be among them.
CMake repeats a library only for a cycle among static libraries, which
GNU ld needs, and for a list out of order; v3's libraries have no cycle,
so a repeat is refused whatever its cause."""

import unittest

from tests.drive import binaries
from foundation.devkit.structure import links

# The links that failed on macOS, by their targets' names.
FIVE = ("qiven3_unit_secret", "qiven3_unit_store_errors", "qiven3_unit_stores", "qiven3-child",
        "qiven3-drive-fake")


def stem(output):
    """A link's target by its output: the file's name, .exe aside."""
    name = output.rsplit("/", 1)[-1]
    return name[:-len(".exe")] if name.endswith(".exe") else name


class Links(unittest.TestCase):
    ctest_labels = ("fake",)
    maxDiff = None  # every line found, named whole

    def test_build(self):
        lines = links.read(binaries.directory("QIVEN3_BUILD"))
        named = {stem(output) for output in lines}
        self.assertEqual([name for name in FIVE if name not in named], [], "a link the check must read")
        self.assertEqual(links.twice(lines), {})
