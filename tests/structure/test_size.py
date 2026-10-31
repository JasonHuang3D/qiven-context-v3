"""B15: no .hpp, .cpp, .py or CMake file over 600 lines, its LFs and one
unended line counted; third_party/sqlite/sqlite3.c, the one vendored file,
is outside the count. Over every file of the checkout, and over planted
files at the bound and past it."""

import tempfile
import unittest
from pathlib import Path

from tests.structure import rules


def size_findings(root):
    out = rules.Findings()
    rules.source_rules(root, rules.source_files(root), out)
    return sorted(f.where for f in out.found if f.rule == "B15")


class Size(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(size_findings(rules.ROOT), [])

    def test_planted(self):
        plants = {
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
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name, text in plants.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_bytes(text.encode("ascii"))
            found = size_findings(root)
        self.assertEqual(found, ["src/x/long.cpp", "src/x/unended.hpp", "tests/x/CMakeLists.txt",
                                 "tests/x/long.cmake", "tests/x/long.py"])
