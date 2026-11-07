"""The exemptions' check of one module, exemptions.<M>: the analysis
configuration files of module M of the table (its .clang-tidy and .ruleset
files, each file under its first owning module) against the list
(exemptions.py, scoped by M's files). harness registers one CTest test
exemptions.<M> for each module that holds such a file (list_tests.py),
which runs Exemptions.test_<M>, below, by its case entry; labelled M and
build, a change to either runs it. The list's whole match is
tests/test_exemptions.py's, build's own."""

import unittest

from foundation.devkit.build import exemptions
from foundation.devkit.python import table as module_table
from foundation.devkit.structure import layers, rules
from foundation.devkit.structure.tree import source_files


def findings(root, module, listing=None):
    """exemptions.findings() of module's files of root's tree; listing, its every file, by default tree.py's."""
    listing = source_files(root) if listing is None else listing
    table = layers.of(root).table
    mine = [rel for rel, names in module_table.owners(table, listing).items() if names[:1] == [module]]
    return exemptions.findings(root, listing, mine)


class Exemptions(unittest.TestCase):
    """exemptions.<M>: M's configuration files and the list agree, a method test_<M> for each module of the table."""


def _method(module):
    def test(self):
        self.assertEqual(findings(rules.ROOT, module), [])
    test.__name__ = f"test_{module}"
    test.__doc__ = f"exemptions.{module}"
    return test


for _name in sorted(m["name"] for m in layers.of(rules.ROOT).table["modules"]):
    setattr(Exemptions, f"test_{_name}", _method(_name))
