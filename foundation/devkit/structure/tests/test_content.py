"""The content rule (content.py) over v3's tree: no finding over every file
of the checkout but those git's ignore files name (tree.py's), with this
machine's paths and names read when the test runs."""

import unittest

from foundation.devkit.python import system
from foundation.devkit.structure import content, rules


@unittest.skipUnless(system.WINDOWS, "the content rule reads this machine's paths, on Windows")
class Content(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(content.content_findings(rules.ROOT, rules.source_files(rules.ROOT), content.machine_paths(),
                                                  content.machine_names()), [])
