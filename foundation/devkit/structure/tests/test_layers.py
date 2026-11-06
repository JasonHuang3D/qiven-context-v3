"""The structure rules' scopes by the tree's table (layers.py), with no
build's records: core logic (B17) by layer, never in devkit's runner, layer
0, nor in the vendored SQLite, layer 2's, nor in test code; memory's
library code core's."""

import unittest

from foundation.devkit.structure import layers, rules

TREE = layers.of(rules.ROOT)
CXX = layers.CXX


class Core(unittest.TestCase):

    def test_outside(self):
        """The runner, devkit's, the vendored SQLite, system's, and test code are outside core logic; memory's
        arena is in it, core's."""
        runner = [rel for rel in rules.source_files(rules.ROOT)
                  if rel.startswith(TREE.directory("runner")) and rel.endswith(CXX)]
        self.assertGreaterEqual(len(runner), 4)
        self.assertEqual(TREE.layer(runner[0]), "devkit")
        self.assertEqual(TREE.layer("foundation/system/sqlite/src/sqlite3.c"), "system")
        for rel in (*runner, "foundation/system/sqlite/src/sqlite3.c", "foundation/system/sqlite/include/qiven/sqlite/sqlite3.h", "foundation/core/memory/tests/arena_test.cpp"):
            with self.subTest(rel=rel):
                self.assertFalse(rules.is_core(rel, TREE))
        self.assertTrue(rules.is_core("foundation/core/memory/src/arena.cpp", TREE))
        self.assertEqual(TREE.layer("foundation/core/memory/src/arena.cpp"), "core")


if __name__ == "__main__":
    unittest.main()
