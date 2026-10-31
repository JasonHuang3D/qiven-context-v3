"""B5 to B11 over every object, target and source file of the build, read
from the structure build's records, and over the planted project, each
plant reported; and the Python sources held to the standard library, read
by ast: no module outside it but the test tree's own, no importlib and no
change of sys.path."""

import ast
import sys
import tempfile
import unittest
from pathlib import Path

from tests.structure import rules
from tests.structure.test_boundaries import WINDOWS, caught, holds

RULES = ("B5", "B6", "B7", "B8", "B9", "B10", "B11")


def python_findings(root, files):
    """(file, what) for each import outside the standard library, the
    test tree and tools/, each importlib and each change of sys.path."""
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
                if top == "importlib" or (top not in sys.stdlib_module_names and top not in ("tests", "tools")):
                    found.append((rel, name))
            if isinstance(node, ast.Attribute) and node.attr == "path" and isinstance(node.value, ast.Name) \
                    and node.value.id == "sys" and not isinstance(node.ctx, ast.Load):
                found.append((rel, "sys.path set"))
            if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Attribute) \
                    and node.value.attr == "path" and isinstance(node.value.value, ast.Name) \
                    and node.value.value.id == "sys":
                found.append((rel, "sys.path." + node.attr))
    return found


@WINDOWS
class Tree(unittest.TestCase):
    """No finding over v3's tree."""


@WINDOWS
class Plant(unittest.TestCase):
    """The planted project's findings."""


for _rule in RULES:
    setattr(Tree, "test_" + _rule.lower(), lambda self, rule=_rule: holds(self, rule))
    setattr(Plant, "test_" + _rule.lower(), lambda self, rule=_rule: caught(self, rule))


class Python(unittest.TestCase):

    def test_tree(self):
        self.assertEqual(python_findings(rules.ROOT, rules.source_files(rules.ROOT)), [])

    def test_planted(self):
        plants = {
            "outside.py": "import json\nimport numpy\n",
            "dotted.py": "from xml.etree import ElementTree\nfrom yaml.loader import Loader\n",
            "importer.py": "import importlib\n",
            "path_insert.py": "import sys\nsys.path.insert(0, 'x')\n",
            "path_set.py": "import sys\nsys.path = []\n",
            "clean.py": "import os\nimport sys\nfrom tests.drive import binaries\nfrom tools import loop\n"
                        "from . import x\n"
                        "s = 'import numpy'\nprint(sys.path[0], os.sep)\n",
        }
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name, text in plants.items():
                (root / name).write_text(text, encoding="utf-8")
            found = python_findings(root, rules.source_files(root))
        self.assertEqual(sorted(found), [
            ("dotted.py", "yaml.loader"), ("importer.py", "importlib"), ("outside.py", "numpy"),
            ("path_insert.py", "sys.path.insert"), ("path_set.py", "sys.path set"),
        ])
