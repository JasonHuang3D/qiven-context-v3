"""The checks of one module, rules.<M>: every rule that a file's own bytes
decide, over the files of one module M of the table, each file under its
first owning module, read against the tree's whole listing (tree.py) where a
rule resolves a name. harness registers one CTest test rules.<M> for each
module of the table (list_tests.py), which runs Check.test_<M>, below, by
its case entry; labelled M and structure, a change to either runs it.

Its parts, a subtest each: the rules, file_rules() of rules.py (B1's text,
B2's, B13's, B14's and B17's tokens, B32's bytes, the subset's B18 to B31
with B26's configuration); the rows, each allowance row of the file half
(rules.half()) whose path holds M's files and no other module's, used by
M's findings; the content check (content.py), on Windows, where it reads
this machine's paths; the Python imports (python_imports.py); B15's size
(size.py); K15 (log_includes.py); and the includes, each C++ file's
include edges and unresolved includes (modules.check, given the subset);
and, on a build that makes the structure records (QIVEN_BUILD), the
direct-include rules on the directives of M's C++ files none of its records
reaches, by name (rules.unreached_rules): another platform's sources and
headers, which no step of this build reads. A change elsewhere that makes
one of M's files reached or unreached, M's own files unchanged, shows at
rules.<M>'s next run or the whole run.
The rules over the records, and every row not one module's, are
tests/test_boundaries.py's; the table, each file's ownership and the link
edges, tests/test_modules.py's."""

import os
import unittest
from pathlib import Path

from foundation.devkit.python import system
from foundation.devkit.python import table as module_table
from foundation.devkit.structure import content, layers, log_includes, modules, python_imports, records, rules, size
from foundation.devkit.structure.tree import source_files


def owners(listing, tree, read=None):
    """{file: its first owning module, or None} of the listing, kept as the
    tree's answers (layers.Layers.module); read, if a dict, takes the table's
    own answer, every owning module of each file."""
    every = module_table.owners(tree.table, listing)
    if read is not None:
        read.update(every)
    first = {rel: next(iter(names), None) for rel, names in every.items()}
    tree.owner.update(first)
    return first


def own_rows(files, listing, owner, tree, module):
    """The rows of the file half whose path holds one of files and no file of another module than module."""
    rows = {row for row in rules.ROWS if "/" in row[1] and rules.half(row) == "file"
            and any(rules.holds(row, rel, tree) for rel in files)}
    return {row for row in rows if all(owner.get(rel) == module for rel in listing if rules.holds(row, rel, tree))}


def findings(root, module, listing=None, machine=True, build=None):
    """{part: [finding]} of module's check over root's tree; listing, the
    tree's every file, by default tree.py's; machine, whether the content
    part runs, reading this machine's paths and names; build, a build
    directory whose structure records the unreached part reads, where it
    makes them."""
    root = Path(root)
    listing = source_files(root) if listing is None else listing
    tree = layers.of(root)
    every = {}
    owner = owners(listing, tree, every)
    files = [rel for rel in listing if owner[rel] == module]
    out = rules.Findings(tree)
    rules.file_rules(root, files, out)
    found = {
        "rules": sorted(out.found),
        "rows": sorted(own_rows(files, listing, owner, tree, module) - out.used, key=repr),
        "imports": python_imports.python_findings(root, files),
        "size": size.size_findings(root, files),
        "K15": log_includes.findings(root, listing, files, tree),
        "includes": modules.check(tree.table, listing, lambda f: (root / f).read_bytes(), subset=files,
                                  whole=False, owner=every),
    }
    if machine:
        found["content"] = content.content_findings(root, files, content.machine_paths(), content.machine_names())
    tree_records = records.tree_records(root, Path(build) / "structure") if build else None
    if tree_records is not None:
        unreached = rules.Findings(tree)
        rules.unreached_rules(tree_records, root, files, unreached)
        found["unreached"] = sorted(unreached.found)
    return found


class Check(unittest.TestCase):
    """rules.<M>: no finding over module M's files of v3's tree, a method test_<M> for each module of the table."""

    def check(self, module):
        found = findings(rules.ROOT, module, machine=system.WINDOWS, build=os.environ.get("QIVEN_BUILD"))
        for part, items in found.items():
            with self.subTest(part=part):
                self.assertEqual(items, [])
        if not system.WINDOWS:
            with self.subTest(part="content"):
                self.skipTest("the content check reads this machine's paths, on Windows")


def _method(module):
    def test(self):
        self.check(module)
    test.__name__ = f"test_{module}"
    test.__doc__ = f"rules.{module}"
    return test


for _name in sorted(m["name"] for m in layers.of(rules.ROOT).table["modules"]):
    setattr(Check, f"test_{_name}", _method(_name))
