"""B15: no .hpp, .cpp, .py or CMake file over 600 lines, its LFs and one
unended line counted; the vendored files, tools/trees.json's vendored, are
outside the count. Over every file of the checkout.

Read from the tree alone, by tree.py, no record, no build and no table, so
that a long file is reported beside a broken table: this file imports
nothing that reads the table, as rules.py does."""

import unittest

from foundation.devkit.structure import layers
from foundation.devkit.structure.tree import line_count, source_files

BOUND = 600


def size_findings(root):
    found = []
    for rel in source_files(root):
        name = rel.rsplit("/", 1)[-1]
        counted = name.endswith((*layers.CXX, ".py", ".cmake")) or name == "CMakeLists.txt"
        if counted and rel not in layers.VENDORED and line_count((root / rel).read_bytes()) > BOUND:
            found.append(rel)
    return sorted(found)


class Size(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(size_findings(layers.ROOT), [])
