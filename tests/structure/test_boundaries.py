"""B1, B2, B12 to B14, B16 and B17 over every object, target and source
file of the build, read from the structure build's records, and over the
planted project: each plant reported, each rule reporting something, and
the clean files, the allowances and the exemptions passing."""

import sys
import unittest

from tests.structure import records, rules

RULES = ("B1", "B2", "B12", "B13", "B14", "B16", "B17")
B17_KINDS = ("include", "os", "name", "mode", "link")
WINDOWS = unittest.skipUnless(sys.platform == "win32", "the structure records are MSVC's, read on Windows")


def found(which, rule):
    return sorted(f for f in rules.findings(which) if f.rule == rule)


def holds(self, rule):
    self.assertEqual(found("tree", rule), [])


def caught(self, rule):
    hits = found("plant", rule)
    self.assertTrue(hits, f"{rule} reports nothing over the planted project")
    for _, where, kind in (p for p in records.PLANTED if p[0] == rule):
        with self.subTest(where=where, kind=kind):
            self.assertTrue(any(f.where == where and kind in ("", f.kind) for f in hits), hits)


@WINDOWS
class Tree(unittest.TestCase):
    """No finding over v3's tree."""


@WINDOWS
class Plant(unittest.TestCase):
    """The planted project's findings."""

    def test_clean(self):
        clean = set(records.CLEAN)
        self.assertEqual(sorted(f for f in rules.findings("plant") if f.where in clean), [])

    def test_b17_kinds(self):
        kinds = {f.kind for f in found("plant", "B17")}
        self.assertEqual(kinds, set(B17_KINDS))


for _rule in RULES:
    setattr(Tree, "test_" + _rule.lower(), lambda self, rule=_rule: holds(self, rule))
    setattr(Plant, "test_" + _rule.lower(), lambda self, rule=_rule: caught(self, rule))
