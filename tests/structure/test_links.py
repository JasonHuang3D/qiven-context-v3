"""964: each library on a link line once. Apple's linker warns that it
ignores a library named twice on one link line, and the macOS build's links
make their warnings errors, so such a line fails there. The build's own
link lines, read from its build.ninja by tests/structure/links.py, hold no
library twice, on every system the build runs on: no job builds macOS
now, and Windows' and Linux's builds stand for it. Every link
the build has is read; the five that failed on macOS must be among them.
CMake repeats a library only for a cycle among static libraries, which
GNU ld needs, and for a list out of order; v3's libraries have no cycle,
so a repeat is refused whatever its cause. The controls: lines planted
with a library twice, in each form a line names libraries in, are
refused; lines naming each once, flags twice among them, pass."""

import unittest

from tests.drive import binaries
from tests.structure import links

# The links that failed on macOS, by their targets' names.
FIVE = ("qiven3_unit_matcher", "qiven3_unit_store_errors", "qiven3_unit_stores", "qiven3-child",
        "qiven3-drive-fake")


def stem(output):
    """A link's target by its output: the file's name, .exe aside."""
    name = output.rsplit("/", 1)[-1]
    return name[:-len(".exe")] if name.endswith(".exe") else name


def planted(rule, output, libraries, kind="EXECUTABLE"):
    """build.ninja's text for one link edge, as CMake's Ninja generator writes it."""
    return (f"build {output}: CXX_{kind}_LINKER__{rule}_Release main.o | lib.a || lib.a\n"
            "  FLAGS = -O2\n"
            f"  LINK_LIBRARIES = {libraries}\n"
            "  OBJECT_DIR = x\n"
            f"  TARGET_FILE = {output}\n\n")


class Links(unittest.TestCase):
    ctest_labels = ("fake",)
    maxDiff = None  # every line found, named whole

    def test_build(self):
        lines = links.read(binaries.directory("QIVEN3_BUILD"))
        named = {stem(output) for output in lines}
        self.assertEqual([name for name in FIVE if name not in named], [], "a link the check must read")
        self.assertEqual(links.twice(lines), {})

    def test_planted_twice(self):
        text = (planted("a", "bin/a", "src/libx.a  src/liby.a  src/libx.a")
                + planted("b", "bin\\b.exe", "src\\x.lib  kernel32.lib  src\\y.lib  KERNEL32.lib")
                + planted("c", "bin/c", "-lm  -pthread  -lm")
                + planted("d", "bin/d", "-framework Foundation  -framework Foundation")
                + planted("e", "lib/libe.so", "src/libx.a  -Wl,-rpath,/x  src/libx.a",
                          "SHARED_LIBRARY")
                + planted("f", "bin/with$ space", '"C:/Program Files/z.lib"  "C:/Program Files/z.lib"')
                + planted("g", "bin/g", "src\\libx.a  src/libx.a"))
        found = links.twice(links.link_lines(text))
        self.assertEqual(found, {"bin/a": ["src/libx.a"], "bin/b.exe": ["kernel32.lib"], "bin/c": ["-lm"],
                                 "bin/d": ["-framework Foundation"], "lib/libe.so": ["src/libx.a"],
                                 "bin/with space": ["c:/program files/z.lib"], "bin/g": ["src/libx.a"]})

    def test_planted_once(self):
        text = (planted("a", "bin/a", "src/libx.a  src/liby.a  -lm  -framework Foundation")
                + planted("b", "bin/b", "-pthread  -Wl,--as-needed  -pthread  -Wl,--as-needed  src/libx.a")
                + planted("c", "bin/c", "")
                + "build src/libx.a: CXX_STATIC_LIBRARY_LINKER__x_Release x.o\n"
                + "  LINK_LIBRARIES = src/liby.a  src/liby.a\n\n"
                + "build x.o: CXX_COMPILER__x_unscanned_Release x.cpp\n  FLAGS = -lm -lm\n")
        lines = links.link_lines(text)
        self.assertEqual(sorted(lines), ["bin/a", "bin/b", "bin/c"])
        self.assertEqual(lines["bin/a"], ["src/libx.a", "src/liby.a", "-lm", "-framework Foundation"])
        self.assertEqual(links.twice(lines), {})

    def test_five_named(self):
        text = "".join(planted(name, f"bin/{name}.exe", "a.lib") for name in FIVE[:4])
        named = {stem(output) for output in links.link_lines(text)}
        self.assertEqual([name for name in FIVE if name not in named], ["qiven3-drive-fake"])
