"""B1, B2, B13, B14 and B17 over every object, target and source file of
the build, read from the structure build's records, and B18 to B31 but
B26, the safe subset's token rules (subset.py), over every source file.
B26 has no token rule: its half here reads the analysis configuration, and
the analysis tests read the rest (test_analysis.py). B12 and B16 have no
rule until backup, forgetting and the map check are built."""

import unittest

from foundation.devkit.python import system
from foundation.devkit.structure import rules

RULES = ("B1", "B2", "B13", "B14", "B17", "B18", "B19", "B20", "B21", "B22", "B23", "B24", "B25", "B26", "B27",
         "B28", "B29", "B30", "B31")
WINDOWS = unittest.skipUnless(system.WINDOWS,"the structure records are MSVC's, read on Windows")


def holds(self, rule):
    self.assertEqual(sorted(f for f in rules.findings() if f.rule == rule), [])


@WINDOWS
class Tree(unittest.TestCase):
    """No finding over v3's tree."""


for _rule in RULES:
    setattr(Tree, "test_" + _rule.lower(), lambda self, rule=_rule: holds(self, rule))
