"""The analysis configuration against its list (exemptions.py): the real
tree's agree. The build's test-code targets against the list's are the
analysis build's, held there (test_analysis.TestTargets)."""

import unittest

from foundation.devkit.build import exemptions
from foundation.devkit.structure import rules

ROOT = rules.ROOT


class Exemptions(unittest.TestCase):
    ctest_labels = ("fake", "fast")

    def test_real_tree(self):
        self.assertEqual(exemptions.findings(ROOT, rules.source_files(ROOT)), [])
