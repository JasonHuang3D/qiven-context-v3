"""B32: no CR byte in a .hpp, .cpp, .py or CMake file, over the files B15
counts; each finding names the file and the first line holding one. Over
every file of the checkout, and over planted files: a CR byte in each kind,
a CRLF and a lone CR, each found with its line, and the clean files of each
kind, a text file and the vendored pair not."""

import tempfile
import unittest
from pathlib import Path

from tests.structure import rules


def line_ending_findings(root):
    out = rules.Findings()
    rules.source_rules(root, rules.source_files(root), out)
    return sorted((f.where, f.detail) for f in out.found if f.rule == "B32")


class LineEndings(unittest.TestCase):
    ctest_reads = (".",)

    def test_tree(self):
        self.assertEqual(line_ending_findings(rules.ROOT), [])

    def test_planted(self):
        plants = {
            "src/x/crlf.hpp": "#pragma once\r\nint x;\r\n",
            "src/x/late.cpp": "//\n//\nint y;\r\n//\r\n",
            "tests/x/lone.py": "#\n#\n#\rx = 1\n",
            "tests/x/CMakeLists.txt": "#\n\r\n",
            "tests/x/crlf.cmake": "#\r\n",
            "src/x/clean.hpp": "#pragma once\n",
            "src/x/clean.cpp": "int z;\n",
            "tests/x/clean.py": "x = 1\n",
            "tests/y/CMakeLists.txt": "#\n",
            "tests/y/clean.cmake": "#",
            "src/x/notes.txt": "x\r\n",
            "third_party/sqlite/sqlite3.c": "/**/\r\n",
            "third_party/sqlite/sqlite3.h": "/**/\r\n",
        }
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name, text in plants.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_bytes(text.encode("ascii"))
            found = line_ending_findings(root)
        self.assertEqual(found, [("src/x/crlf.hpp", "line 1: a CR byte"), ("src/x/late.cpp", "line 3: a CR byte"),
                                 ("tests/x/CMakeLists.txt", "line 2: a CR byte"),
                                 ("tests/x/crlf.cmake", "line 1: a CR byte"),
                                 ("tests/x/lone.py", "line 3: a CR byte")])
