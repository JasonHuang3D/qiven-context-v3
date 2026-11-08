"""The control of the allowance rows the structure steps use. The build of
a tree with the records (QIVEN_STRUCTURE; steps.py) applies B1, B2, B5 to
B11, B13, B14 and B17 to every object, target and link edge, a finding
failing it; each target's step and the link step write the rows they used
beside their output. The directives of the files no record reaches are
each module's check's, by name (rules.unreached_rules). The rules each file's own bytes decide, B18 to B32
among them, are each module's check's (rules.<M>,
foundation/devkit/structure/checks.py). B12 and B16 have no rule until
backup, forgetting and the map check are built. The test, over the build
ctest names (QIVEN_BUILD), controls the rows no module's check holds
(rules.ROWS): each row of the records' half, each row of both halves, its
files' own rules run too, each file row whose path holds more than one
module's files or none; a row that allowed nothing fails by name."""

import json
import os
import unittest
from pathlib import Path

from foundation.devkit.python import build_tree, system
from foundation.devkit.structure import checks, records, rules


def tree_rows(root, files, tree):
    """The rows this test controls: every row but the file rows whose path holds files of exactly one module."""
    owner = checks.owners(files, tree)
    held = rules.row_files(files, tree)
    return {row for row in rules.ROWS
            if rules.half(row) != "file" or len({owner[rel] for rel in held[row]}) != 1}, held


def steps_used(out):
    """The rows the build's steps used: each target's of the compile commands, and the link step's."""
    targets = {records.target_of(e["output"])
               for e in json.loads((out / "compile_commands.json").read_text(encoding="utf-8"))}
    written = [out / "links.json"] + [out / "targets" / (t + ".json") for t in sorted(targets)]
    return {tuple(row) for path in written if path.is_file()
            for row in json.loads(path.read_text(encoding="utf-8"))["used"]}


@unittest.skipUnless(system.WINDOWS, "the structure records are MSVC's, made on Windows")
class Tree(unittest.TestCase):
    """Each row allows something over v3's tree."""

    def test_rules(self):
        build = Path(os.environ["QIVEN_BUILD"])
        if str((build_tree.options(build) or {}).get("QIVEN_STRUCTURE", "")).upper() not in ("ON", "TRUE", "1"):
            self.skipTest("the build makes no structure records (QIVEN_STRUCTURE)")
        root = rules.ROOT
        files = rules.source_files(root)
        tree = rules.Findings().tree
        rows, held = tree_rows(root, files, tree)
        aside = rules.Findings(tree)  # the file half over the files of this test's rows, for their use alone
        rules.file_rules(root, sorted({rel for row in rows if row in held for rel in held[row]}), aside)
        # the directives no record reaches, each module's check's (checks.py), over every module
        rules.unreached_rules(records.tree_records(root, build / "structure"), root, files, aside)
        used = steps_used(build / "structure")
        self.assertEqual(sorted(rows - used - aside.used, key=repr), [])
