"""B15: no .hpp, .cpp, .py or CMake file over 600 lines, its LFs and one
unended line counted; the vendored files, tools/trees.json's vendored, are
outside the count. size_findings() applies it to every file of a checkout;
tests/test_size.py, to v3's.

Read from the tree alone, its files by tree.py through git, no record, no
build and no table, so that a long file is reported beside a broken table:
this file and its test import nothing that reads the table, as rules.py
does."""

from foundation.devkit.structure import layers
from foundation.devkit.structure.tree import source_files

BOUND = 600


def line_count(data):
    """The LFs of data, and one unended line."""
    return data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)


def size_findings(root):
    found = []
    for rel in source_files(root):
        name = rel.rsplit("/", 1)[-1]
        counted = name.endswith((*layers.CXX, ".py", ".cmake")) or name == "CMakeLists.txt"
        if counted and rel not in layers.VENDORED and line_count((root / rel).read_bytes()) > BOUND:
            found.append(rel)
    return sorted(found)
