"""Ninja's report of a build, against a real Ninja on a build file of the
test's own whose rules run no compiler: a build read as nothing rebuilt when
a step ran, or a step that ran left out, would run no test for a change or
select by the wrong steps."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

from qiven_devkit.wrap import ninja

LIMIT = 120
RULES = """\
rule partial
  command = cmd /c "<nul set /p=no-line-end& copy /y $in $out >nul"
  description = PARTIAL $out
rule copy
  command = cmd /c copy /y $in $out >nul
  description = COPY $out
build a.txt: partial in-a.txt
build b.txt: copy a.txt
build c.txt: copy in-c.txt
"""


@unittest.skipUnless(sys.platform == "win32", "wrap runs on Windows only")
class Report(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.tree = Path(scratch.name)
        (self.tree / "build.ninja").write_text(RULES, encoding="utf-8")
        for name in ("in-a.txt", "in-c.txt"):
            (self.tree / name).write_text("x\n", encoding="utf-8")
        self.ninja = ninja.Ninja("ninja", dict(os.environ), LIMIT)

    def later(self, name):
        """name's content changed, its time moved past every output's."""
        file = self.tree / name
        file.write_text("y\n", encoding="utf-8")
        stamp = max(os.stat(self.tree / out).st_mtime for out in ("a.txt", "b.txt", "c.txt")) + 10
        os.utime(file, (stamp, stamp))

    def test_first_build_names_each_step(self):
        """Each step named, the one after a command's output that has no
        line end included; that output kept among what Ninja said."""
        built = self.ninja.build(self.tree)
        self.assertEqual(sorted(built.steps), ["COPY b.txt", "COPY c.txt", "PARTIAL a.txt"])
        self.assertLess(built.steps.index("PARTIAL a.txt"), built.steps.index("COPY b.txt"))
        self.assertFalse(built.nothing)
        self.assertTrue(any("no-line-end" in line for line in built.said))

    def test_nothing_to_do_reads_as_nothing(self):
        self.ninja.build(self.tree)
        built = self.ninja.build(self.tree)
        self.assertEqual(built.steps, [])
        self.assertTrue(built.nothing)

    def test_changed_input_names_what_ran(self):
        """A changed input names the steps it reran and no other."""
        self.ninja.build(self.tree)
        self.later("in-c.txt")
        self.assertEqual(self.ninja.build(self.tree).steps, ["COPY c.txt"])
        self.later("in-a.txt")
        built = self.ninja.build(self.tree, ["b.txt"])
        self.assertEqual(built.steps, ["PARTIAL a.txt", "COPY b.txt"])
        self.assertFalse(built.nothing)


if __name__ == "__main__":
    unittest.main()
