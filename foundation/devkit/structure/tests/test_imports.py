"""B5 to B11 over every object, target and source file of the build, read
from the structure build's records; and the Python sources held to the
standard library, read by ast: no module outside it but the test tree's
own, the structure method's, the runner's Python half and Python's lowest
layer; no importlib or runpy, every module imported normally, none loaded
or run by its path; no ctypes, winreg or _winapi but in system's Windows
back end, foundation/devkit/python/_windows.py, the one module wrapping
them; and no change of sys.path but the one form a script
under -I needs for that layer, sys.path.append of the root, so that the
standard library still wins."""

import ast
import sys
import unittest

from foundation.devkit.structure import rules
from foundation.devkit.structure.tests.test_boundaries import WINDOWS, holds

RULES = ("B5", "B6", "B7", "B8", "B9", "B10", "B11")
METHOD = "foundation.devkit.structure"  # the structure method's own package, beside the test tree
RUNNER = "foundation.devkit.test.runner"  # the runner's Python half: a run's skip, the build's binaries and options, the listing
LOWEST = "foundation.devkit.python"  # Python's lowest layer


def python_findings(root, files):
    """(file, what) for each import outside the standard library, the
    test tree, the structure method's own package, the runner's Python
    half and Python's lowest layer, each importlib or runpy, each ctypes,
    winreg and _winapi outside _windows.py and each change of sys.path but
    sys.path.append."""
    found = []
    for rel in files:
        if not rel.endswith(".py"):
            continue
        tree = ast.parse((root / rel).read_bytes(), rel)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.level == 0:
                names = [node.module]
            else:
                names = []
            for name in names:
                top = name.split(".")[0]
                own = any(name == p or name.startswith(p + ".") for p in (METHOD, RUNNER, LOWEST))
                wrapped = top in ("ctypes", "winreg", "_winapi") and rel != "foundation/devkit/python/_windows.py"
                if top in ("importlib", "runpy") or wrapped or (top not in sys.stdlib_module_names
                                                                and top != "tests" and not own):
                    found.append((rel, name))
            if isinstance(node, ast.Attribute) and node.attr == "path" and isinstance(node.value, ast.Name) \
                    and node.value.id == "sys" and not isinstance(node.ctx, ast.Load):
                found.append((rel, "sys.path set"))
            if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Attribute) \
                    and node.value.attr == "path" and isinstance(node.value.value, ast.Name) \
                    and node.value.value.id == "sys" and node.attr != "append":
                found.append((rel, "sys.path." + node.attr))
    return found


@WINDOWS
class Tree(unittest.TestCase):
    """No finding over v3's tree."""


for _rule in RULES:
    setattr(Tree, "test_" + _rule.lower(), lambda self, rule=_rule: holds(self, rule))


class Python(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(python_findings(rules.ROOT, rules.source_files(rules.ROOT)), [])
