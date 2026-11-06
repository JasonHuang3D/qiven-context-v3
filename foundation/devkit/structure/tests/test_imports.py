"""The Python import check (python_imports.py) over v3's tree: every Python
source held to the standard library and the layers it names."""

import unittest

from foundation.devkit.structure import python_imports, rules


class Python(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(python_imports.python_findings(rules.ROOT, rules.source_files(rules.ROOT)), [])
