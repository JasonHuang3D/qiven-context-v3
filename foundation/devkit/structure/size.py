"""B15: no .hpp, .cpp, .py or CMake file over 600 lines, its LFs and one
unended line counted; the vendored files, tools/trees.json's vendored, are
outside the count. size_findings() applies it to a set of a checkout's
files; checks.py, to each module's.

Read from the files alone, no record and no build, and with no table of
its own: the files are given."""

from foundation.devkit.structure import layers

BOUND = 600


def line_count(data):
    """The LFs of data, and one unended line."""
    return data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)


def size_findings(root, files):
    """The files of files, relative to root, past the bound."""
    found = []
    for rel in files:
        name = rel.rsplit("/", 1)[-1]
        counted = name.endswith((*layers.CXX, ".py", ".cmake")) or name == "CMakeLists.txt"
        if counted and rel not in layers.VENDORED and line_count((root / rel).read_bytes()) > BOUND:
            found.append(rel)
    return sorted(found)
