"""The one list of C++ suffixes, tools/trees.json's, in each rule and walker: a planted file of every suffix
read, a file of no suffix on the list left, the floors' .rc read beside it. K15's case is in
test_log_includes.py, the modules check's in tests/drive/test_loop_*.py, the build's in test_trees_build.py."""

import runpy
import types
import unittest
from pathlib import Path
from unittest import mock

from foundation.devkit.structure import layers, records, rules
from foundation.devkit.structure.tests.test_floors import tree_sources
from foundation.devkit.structure.tests.test_layers import PLANT_TABLE, planted, subset_findings

SNAPSHOT = runpy.run_path(str(rules.ROOT / "tools" / "nav.py"))["snapshot"]
CODE = sorted(f"src/x{suffix}" for suffix in layers.CXX)


def fake_git(root, *argv, data=None):
    """git's answers to nav's snapshot: its head, the files of src/, a blob for each."""
    if argv[0] == "ls-files":
        return "\0".join(p.relative_to(root).as_posix() for p in Path(root, "src").iterdir())
    return "head" if data is None else "blob\n" * (data.count(b"\n") + 1)


class Suffixes(unittest.TestCase):
    ctest_labels = ("fake",)

    def tree(self, text, other=".txt"):
        """A scratch tree under a table of a file of text for each of CODE and of the suffix other, its root."""
        return planted(self, {**dict.fromkeys((*CODE, f"src/x{other}"), text), "products/plant/module.json": PLANT_TABLE})

    def test_rules(self):
        """B2's strings, an include's rules and the subset's tokens, in each file of the list alone."""
        for rule, walk, text in (("B2", rules.source_rules, 'const char* s = "rev-parse";\n'), ("B1", lambda *given: (
                rules.directive_rules(types.SimpleNamespace(objects=()), *given)), '#include "sqlite3.h"\n'),
                                 ("B15", rules.source_rules, "\n" * 601)):
            root = self.tree(text)
            out = rules.Findings(layers.of(root))
            walk(root, rules.source_files(root), out)
            self.assertEqual(sorted(f.where for f in out.found if f.rule == rule), CODE)
        found = subset_findings(self.tree("int* p = new int;\n"))
        self.assertEqual(sorted(where for rule, where, _, _ in found if rule == "B19"), CODE)

    def test_instantiation(self):
        """An instantiation file: a source of every suffix, no header."""
        self.assertEqual(tuple(s for s in layers.CXX if rules.is_instantiation(f"src/x/instances{s}")), layers.SOURCES)

    def test_walkers(self):
        """The records' digests, the navigator's snapshot, and the floors' sources with .rc."""
        root = self.tree("", other=".rc")
        self.assertEqual(sorted(records.digests(root)), CODE)
        self.assertEqual(sorted(name for name, _ in tree_sources(root)), sorted([*CODE, "src/x.rc"]))
        with mock.patch.dict(SNAPSHOT.__globals__, git=fake_git):
            self.assertEqual(sorted(SNAPSHOT(root)[1]), CODE)
