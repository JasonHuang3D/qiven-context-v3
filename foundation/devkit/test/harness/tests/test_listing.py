"""The listing (list_tests.py) and CTest's start of each Python test it lists
(ctest_tests.cmake): the packages it discovers in, the directories of tests
discover would pass over, each listed test run from its top package, and a
test's package by the one reader of test code."""

import re
import sys
import tempfile
import unittest
from pathlib import Path

from foundation.devkit.python import process
from foundation.devkit.python import table
from foundation.devkit.python.build_tree import ROOT, cmake
from foundation.devkit.test.harness import list_tests

TREES = table.trees(ROOT)
HOMES = {home.rpartition("/")[0] for home in table.tree(ROOT)[0]["homes"].values()}


class Stopped(Exception):
    """A tool that had to succeed failed: its Ran, the one argument."""


def succeeded(argv, limit=process.LIMIT):
    """Runs a tool that must succeed: Ran, its output as text; else Stopped."""
    ran = process.run(argv, limit=limit, text=True)
    if ran.status != 0:
        raise Stopped(ran)
    return ran


class Listing(unittest.TestCase):
    ctest_labels = ("fake",)  # the fake selection runs it

    def test_discovery(self):
        """The listing discovers in every top package of the walked trees and
        the layers': a module's own tests/ package under a layer is found,
        and a directory of tests with no __init__.py is named."""
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for init in ("tests/__init__.py", "tests/drive/__init__.py", "foundation/core/m/tests/__init__.py",
                         "foundation/core/m/tests/__pycache__/__init__.py", "kits/k/x/__init__.py"):
                (root / init).parent.mkdir(parents=True, exist_ok=True)
                (root / init).write_text("", encoding="utf-8")
            found = list_tests.packages(root, ("tests", "tools", "foundation", "kits"))
            self.assertEqual([p.relative_to(root).as_posix() for p in found],
                             ["foundation/core/m/tests", "kits/k/x", "tests"])
            self.assertEqual(list_tests.unpackaged(root, ("tests", "foundation", "kits")), [])
            for test in ("kits/k/tests/test_p.py", "tests/drive/test_q.py", "tests/plain/test_r.py"):
                (root / test).parent.mkdir(parents=True, exist_ok=True)
                (root / test).write_text("", encoding="utf-8")
            self.assertEqual([p.relative_to(root).as_posix() for p in list_tests.unpackaged(root, ("tests", "kits"))],
                             ["kits/k/tests", "tests/plain"])

    def test_ctest_start(self):
        """CTest runs each listed Python test by its id from its top package:
        one under tests/ as discover -s tests, one moved into a module's own
        tests/ from there; both run and pass."""
        tool = cmake()
        if tool is None:
            raise unittest.SkipTest("no cmake")
        starts = {"tests.drive.test_a.A.test_a": "tests",
                  "foundation.core.m.tests.test_b.B.test_b": "foundation/core/m/tests"}
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            lines = "".join(f"{test_id}\tdrive\n" for test_id in starts)
            files = {"tests/__init__.py": "", "tests/drive/__init__.py": "", "foundation/core/m/tests/__init__.py": "",
                     "foundation/devkit/test/harness/list_tests.py": f"print({lines!r}, end='')\n", "inputs.txt": ""}
            for test_id, start in starts.items():
                module, cls, method = test_id.rsplit(".", 2)
                files[module.replace(".", "/") + ".py"] = (f"import unittest\nclass {cls}(unittest.TestCase):\n"
                                                           f"    def {method}(self):\n        pass\n")
            for name, text in files.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text, encoding="utf-8")
            out = root / "out.cmake"
            args = [tool, f"-DINPUTS={root / 'inputs.txt'}", f"-DOUT={out}", f"-DSOURCE={root.as_posix()}", "-DBIN=b",
                    "-DBUILD=b", "-DCTEST=c", "-DSKIP=77", f"-DPYTHON={sys.executable}",
                    "-P", str(ROOT / "foundation" / "devkit" / "test" / "harness" / "ctest_tests.cmake")]
            succeeded(args)
            added = re.findall(r"(?m)^add_test\((.*)\)$", out.read_text(encoding="utf-8"))
            self.assertEqual(len(added), len(starts))
            for line in added:
                name, *words = re.findall(r"\[==\[(.*?)\]==\]", line)
                command = words[words.index("--") + 1:]
                self.assertEqual(command[1:], ["-I", "-m", "unittest", "discover", "-v", "-s", starts[name], "-t",
                                               ".", "-k", f"*{name}"])
                done = process.run([sys.executable, *command[1:]], cwd=root, text=True)
                self.assertEqual(done.status, 0, done.err)
                self.assertIn("Ran 1 test", done.err)

    def test_listing_reader(self):
        """The listing takes a Python test's package by the one reader: none
        where the reader says it is no test code, a module's src/tests/ among
        them; else the package below tests/, as the top-level tree holds it."""
        paths = {"kits/ai/src/tests/test_x.py": None, "foundation/core/memory/src/tests/test_x.py": None,
                 "tools/tests/test_x.py": "tests", "tests/test_x.py": None, "tests/drive/test_x.py": "drive",
                 "foundation/core/memory/tests/test_x.py": "tests",
                 "foundation/core/memory/tests/structure/test_x.py": "structure"}
        files = [p.relative_to(ROOT).as_posix() for tree in TREES.every for p in (ROOT / tree).rglob("test_*.py")]
        got = {path: list_tests.package(path, TREES, HOMES) for path in [*files, *paths]}
        self.assertEqual({path: got[path] for path in paths}, paths)
        self.assertEqual([p for p, kind in got.items() if kind is None and TREES.test_path(p, HOMES)],
                         ["tests/test_x.py"])
        self.assertEqual([p for p, kind in got.items() if kind and not TREES.test_path(p, HOMES)], [])
