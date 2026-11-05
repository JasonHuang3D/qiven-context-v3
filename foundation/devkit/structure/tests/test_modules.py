"""The modules' check whole over the real checkout, as tools/loop_modules.py
runs it: the table files, each table directory's README.md, each contract,
each tracked file's module, each include edge and, given the build tree
ctest names (QIVEN_BUILD), each link edge by its codemodel."""

import os
import unittest

from foundation.devkit.python import git
from foundation.devkit.structure import layers, modules


class FullCheck(unittest.TestCase):
    ctest_labels = ("fake",)  # the selection CI runs

    def test_tree(self):
        """No error over the checkout and, under ctest, its build tree; the
        README.md lines read, each table directory's."""
        root, build = layers.ROOT, os.environ.get("QIVEN_BUILD")
        files = git.tracked(root)  # as run() takes them, one git process
        table, errors = modules.tree(root, files)
        targets = modules.codemodel(build, root) if build else None
        errors += modules.check(table, files, lambda f: (root / f).read_bytes(), targets, [])
        self.assertEqual(errors, [])
        readmes = [p.replace("module.json", "README.md") for p in table["trees"].table_files(root)]
        self.assertTrue(readmes and all(p in files for p in readmes), [p for p in readmes if p not in files])


if __name__ == "__main__":
    unittest.main()
