"""A tree's modules and their layers, as tools/loop_modules.py reads the
module table: a file's module, the first row whose paths hold it; that
module's layer, its table file's directory's in the tree (foundation/core/
gives core, products/ products); and test code, a file under the top-level
tests/ or under a module directory's tests/. The structure rules read their
scopes here: core logic (B17) by layer, the safe subset's files (B19 to
B31) and the log's includes (K15); the walked trees, tools/trees.json's; and
B1's text, layered().
"""

import runpy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
# The table's reader, by runpy: run as a script under -I, the checkout is off sys.path.
MODULES = runpy.run_path(str(ROOT / "tools" / "loop_modules.py"))
TREES = tuple(MODULES["TREES"])  # tools/trees.json's walked and layers: every tree the checks walk
SOURCES, HEADERS, CXX = MODULES["SOURCE_SUFFIXES"], MODULES["HEADER_SUFFIXES"], MODULES["CXX_SUFFIXES"]  # its suffixes
LINKED = tuple(MODULES["ORDER"])  # the link graph's layers, devkit first
CORE = LINKED[1:]  # core logic's layers: the link graph's but devkit, layer 0, linked into tests alone
KIT = MODULES["KIT"]  # the test kit's table files
VENDORED = MODULES["VENDORED"]  # a vendor's own files, tools/trees.json's vendored; not their module's other files
TESTS = f"{MODULES['TESTS']}/"  # the top-level test tree
SUPPORT = f"{MODULES['TREE_FILE']['support']}/"  # the tests' shared support, in the subset with the libraries
CHILD = f"{MODULES['TREE_FILE']['child']}/"  # the test tree's child processes
TOOLS = "tools/"  # the tools' tree: no layer's code, whichever row holds its files
ANALYSED = tuple(f"{root}/" for root in MODULES["TREE_FILE"]["analysed"])  # the analysis preset's roots
LAYER_TREES = tuple(f"{layer}/" for layer in MODULES["TREE_FILE"]["layers"])  # the layers' trees


def walked(rel):
    """Whether rel is under one of the trees every check walks."""
    return any(rel.startswith(tree + "/") for tree in TREES)


def layered(rel):
    """Whether rel is of the layers' code or test code, by where it lies, so
    that a file of no row and a tool's test stay in, and a module keeps its
    place as it moves: a walked tree's but the tools' and a vendored file."""
    return walked(rel) and not rel.startswith(TOOLS) and rel not in VENDORED


class Layers:
    """One tree's table, read for the rules: each path's module, layer and
    whether it is test code, each answer kept."""

    def __init__(self, table):
        self.table = table
        homes = table["homes"]
        self.place = {name: MODULES["tree_layer"](home.rpartition("/")[0]) for name, home in homes.items()}
        self.homes = {name: home.rpartition("/")[0] + "/" for name, home in homes.items()}
        self.dirs = {home.rpartition("/")[0] for home in homes.values()}
        self.owner = {}

    def module(self, rel):
        """The name of the first module whose paths hold rel, or None."""
        if rel not in self.owner:
            self.owner[rel] = next(iter(MODULES["owners"](self.table, [rel])[rel]), None)
        return self.owner[rel]

    def layer(self, rel):
        """rel's module's layer in the tree, or None for a file of no module."""
        return self.place.get(self.module(rel)) if rel is not None else None

    def test_code(self, rel):
        return MODULES["under_tests"](rel, self.dirs)

    def analysed(self, rel):
        """Whether the analysis reads rel, as trees.cmake's qiven3_analysed
        rules: under an analysed root, as it stands or as the top-level
        tests/ would hold it, or under a layer's tree and no test code; never
        a vendored file."""
        if rel in VENDORED:
            return False
        test = MODULES["test_path"](rel, self.dirs)
        return any(where.startswith(root) for where in (rel, test or "") for root in ANALYSED) or (
            test is None and rel.startswith(LAYER_TREES))

    def as_tests(self, rel):
        """rel as the top-level tests/ would hold it: a module's own test
        file keeps the rules of the tree it moved from. Any other path, or
        a target's or a header's name, as it stands."""
        return MODULES["test_path"](rel, self.dirs) or rel if rel is not None else None

    def directory(self, name):
        """A module's table file's directory, ending in /."""
        return self.homes[name]

    def library(self, rel):
        """Whether rel is a library's file of the link graph's core logic
        layers: core to products, neither test code nor a vendored file."""
        return (rel is not None and rel not in VENDORED and not self.test_code(rel)
                and self.layer(rel) in CORE)


_READ = {}


def of(root):
    """The Layers of a tree's own table, read once per tree; a tree of no
    table files has none, and every file is of no module."""
    root = Path(root).resolve()
    if root not in _READ:
        _READ[root] = Layers(MODULES["tree"](root)[0])
    return _READ[root]


def read(root):
    """(the Layers of a tree's own table, the errors its table holds), for a
    reader that must refuse what of() reads on."""
    return of(root), MODULES["tree"](Path(root).resolve())[1]
