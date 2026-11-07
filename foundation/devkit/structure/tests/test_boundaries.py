"""B1, B2, B5 to B11, B13, B14 and B17 over every object, target and
source file of the build, read from the structure build's records
(rules.record_rules: the includes, the directives, the names and the links):
one test, which runs the check once, a subtest a rule. The rules each file's
own bytes decide, B18 to B32 among them, are each module's check's
(rules.<M>, foundation/devkit/structure/checks.py). B12 and B16 have no rule
until backup, forgetting and the map check are built. A last subtest is the
control of the rows no module's check holds (rules.ROWS): each row of the
records' half, each row of both halves, its files' own rules run too, each
file row whose path holds more than one module's files or none; a row that
allowed nothing fails by name."""

import unittest

from foundation.devkit.python import system
from foundation.devkit.structure import checks, rules

RULES = ("B1", "B2", "B5", "B6", "B7", "B8", "B9", "B10", "B11", "B13", "B14", "B17")


def tree_rows(root, files, tree):
    """The rows this test controls: every row but the file rows whose path holds files of exactly one module."""
    owner = checks.owners(files, tree)
    held = rules.row_files(files, tree)
    return {row for row in rules.ROWS
            if rules.half(row) != "file" or len({owner[rel] for rel in held[row]}) != 1}, held


@unittest.skipUnless(system.WINDOWS, "the structure records are MSVC's, read on Windows")
class Tree(unittest.TestCase):
    """No finding over v3's tree."""

    def test_rules(self):
        root, records = rules.ROOT, rules.current_records()
        files = rules.source_files(root)
        out = rules.Findings()
        rules.record_rules(records, root, files, rules.goldens_of(root, files), out)
        rows, held = tree_rows(root, files, out.tree)
        aside = rules.Findings(out.tree)  # the file half over the files of this test's rows, for their use alone
        rules.file_rules(root, sorted({rel for row in rows if row in held for rel in held[row]}), aside)
        for rule in RULES:
            with self.subTest(rule=rule):
                self.assertEqual(sorted(f for f in out.found if f.rule == rule), [])
        with self.subTest(allowances="each allowed something"):
            self.assertEqual(sorted(rows - out.used - aside.used, key=repr), [])
