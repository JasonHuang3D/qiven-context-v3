"""The modules' check by its command line, tools/loop_modules.py, over the
real checkout and, under ctest, the build tree it names (QIVEN_BUILD): both
halves, the table's and each file's include edges, no error. A hand step of
the gates' list until the gates landed as data (tools/gates.json); now a
test of the tree, by its module's label alone, tooling: a change to the
check or the table's reader runs structure's test_modules and each
module's rules.<M>, and every gate's whole run runs this one."""

import os
import sys
import unittest

from foundation.devkit.python import process
from foundation.devkit.python.build_tree import ROOT

COMMAND = ROOT / "tools" / "loop_modules.py"
LIMIT = 240  # seconds; under a second here, measured once on build/ci-windows


class LoopModules(unittest.TestCase):
    def test_tree(self):
        """Exit 0 and its last line "modules: 0 errors"."""
        build = os.environ.get("QIVEN_BUILD")
        done = process.run([sys.executable, "-I", COMMAND, str(ROOT), *([build] if build else [])], cwd=ROOT,
                           limit=LIMIT, err=process.MERGE, text=True)
        lines = done.out.splitlines()
        self.assertEqual((done.status, lines[-1:]), (0, ["modules: 0 errors"]), done.out[-4000:])


if __name__ == "__main__":
    unittest.main()
