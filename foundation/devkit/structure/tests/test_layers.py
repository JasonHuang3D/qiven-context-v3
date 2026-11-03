"""The structure rules' scopes by the tree's table (layers.py), with no
build's records: core logic (B17) by layer, held in every file of src/ it
held when src/ was its literal root, never in devkit's runner, layer 0, nor
in the vendored SQLite, layer 2's, nor in test code; B9's own headers a
library's."""

import unittest

from foundation.devkit.structure import layers, rules

TREE = layers.of(rules.ROOT)
CXX = layers.CXX
# Where core logic was read: src/, and memory's, matcher's and compute's library code, in src/ until their
# moves (compute's fake, a library's since its move from tests/support/, with it).
OLD_SRC = ("src/", "foundation/core/memory/include/", "foundation/core/memory/src/",
           "foundation/core/matcher/include/", "foundation/core/matcher/src/",
           "foundation/services/compute/include/", "foundation/services/compute/src/")


def files():
    return rules.source_files(rules.ROOT)


def was_core(rel):
    """Core logic as it was read, src/ its literal root, memory's, matcher's and compute's code at their new
    places."""
    return rel.startswith(OLD_SRC) and not rules.under(rel, rules.NOT_CORE) and not rules.is_instantiation(rel)


class Core(unittest.TestCase):

    def test_kept(self):
        """Each file core logic held with src/ its root, it holds."""
        held = [rel for rel in files() if rel.endswith(CXX) and was_core(rel)]
        self.assertGreater(len(held), 100)
        self.assertEqual([rel for rel in held if not rules.is_core(rel, TREE)], [])

    def test_outside(self):
        """No file outside src/ and memory's, matcher's and compute's code is core logic today: the runner, devkit's,
        the vendored SQLite, system's, and test code are outside it."""
        self.assertEqual([rel for rel in files() if rel.endswith(CXX) and rules.is_core(rel, TREE)
                          and not was_core(rel)], [])
        runner = [rel for rel in files() if rel.startswith(rules.RUNNER) and rel.endswith(CXX)]
        self.assertGreaterEqual(len(runner), 4)
        self.assertEqual(TREE.layer(runner[0]), "devkit")
        self.assertEqual(TREE.layer("foundation/system/sqlite/src/sqlite3.c"), "system")
        for rel in (*runner, "foundation/system/sqlite/src/sqlite3.c", "foundation/system/sqlite/include/qiven/sqlite/sqlite3.h", "foundation/core/memory/tests/arena_test.cpp"):
            with self.subTest(rel=rel):
                self.assertFalse(rules.is_core(rel, TREE))
        self.assertTrue(rules.is_core("foundation/core/memory/src/arena.cpp", TREE))
        self.assertEqual(TREE.layer("foundation/core/memory/src/arena.cpp"), "core")

    def test_b9_libraries(self):
        """B9's own headers: a library's of core to products, as src/ was."""
        self.assertEqual([rel for rel in files() if TREE.library(rel) != rel.startswith(OLD_SRC)
                          and rel.endswith(CXX)], [])


if __name__ == "__main__":
    unittest.main()
