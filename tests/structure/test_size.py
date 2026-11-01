"""B15: no .hpp, .cpp, .py or CMake file over 600 lines, its LFs and one
unended line counted; third_party/sqlite/sqlite3.c, the one vendored file,
is outside the count. Over every file of the checkout, and over planted
files at the bound and past it.

Read from the tree alone, by tree.py, no record, no build and no table, so
that the loop's size step reports a long file beside a broken table
(tools/loop_jobs.py): this file imports nothing that reads the table, as
rules.py does. test_size_rule.py holds rules.py's B15 to the same plants."""

import tempfile
import unittest
from pathlib import Path

from tests.structure import layers
from tests.structure.tree import line_count, source_files

BOUND = 600
PLANTS = {
    "src/x/long.cpp": "//\n" * 601,
    "src/x/unended.hpp": "//\n" * 600 + "int x;",
    "tests/x/long.py": "#\n" * 601,
    "tests/x/CMakeLists.txt": "#\n" * 601,
    "tests/x/long.cmake": "#\n" * 600 + "#",
    "src/x/edge.cpp": "//\n" * 600,
    "src/x/edge.py": "#\n" * 599 + "#",
    "src/x/long.txt": "x\n" * 700,
    "third_party/sqlite/sqlite3.c": "/**/\n" * 700,
}
PAST = ["src/x/long.cpp", "src/x/unended.hpp", "tests/x/CMakeLists.txt", "tests/x/long.cmake", "tests/x/long.py"]


def write_plants(root):
    for name, text in PLANTS.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(text.encode("ascii"))


def size_findings(root):
    found = []
    for rel in source_files(root):
        name = rel.rsplit("/", 1)[-1]
        counted = name.endswith((*layers.CXX, ".py", ".cmake")) or name == "CMakeLists.txt"
        if counted and not rel.startswith(layers.VENDORED) and line_count((root / rel).read_bytes()) > BOUND:
            found.append(rel)
    return sorted(found)


class Size(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(size_findings(layers.ROOT), [])

    def test_planted(self):
        with tempfile.TemporaryDirectory() as scratch:
            write_plants(Path(scratch))
            self.assertEqual(size_findings(Path(scratch)), PAST)
