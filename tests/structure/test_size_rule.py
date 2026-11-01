"""B15 in rules.py's checker finds what test_size.py's reading of the tree
finds, over the same plants: the one reads the table, the other none, so
that the loop's size step runs beside a broken table."""

import tempfile
import unittest
from pathlib import Path

from tests.structure import rules
from tests.structure.test_size import PAST, write_plants


class Rule(unittest.TestCase):

    def test_same_plants(self):
        out = rules.Findings()
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            write_plants(root)
            rules.source_rules(root, rules.source_files(root), out)
        self.assertEqual(sorted(f.where for f in out.found if f.rule == "B15"), PAST)
