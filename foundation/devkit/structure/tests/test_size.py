"""B15 (size.py) over every file of v3's checkout, read from the tree
alone: this file imports nothing that reads the table, as rules.py does."""

import unittest

from foundation.devkit.structure import layers, size


class Size(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(size.size_findings(layers.ROOT), [])
