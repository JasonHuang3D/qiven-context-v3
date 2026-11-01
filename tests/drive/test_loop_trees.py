"""tools/trees.json in the loop's tools: the modules check's include roots
read from it (its included, each under a tree it walks), and a change to it
choosing every structure test (tools/loop_select.py), whose rules take
their scopes from it, with the tooling's tests, whose file it is, and no
other module's."""

import json
import unittest

from tests.drive.test_loop_ci import load_loop
from tools import loop_modules

SELECT = load_loop().SELECT


class Roots(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_included(self):
        """The include roots, tools/trees.json's included, each under a
        tree every check walks; include_roots() starts from them."""
        trees = json.loads((loop_modules.ROOT / "tools" / "trees.json").read_text(encoding="utf-8"))
        self.assertEqual(loop_modules.ROOTS, tuple(trees["included"]))
        self.assertEqual(loop_modules.ROOTS, ("src", "tests", "third_party/sqlite"))
        self.assertTrue(all(r.split("/", 1)[0] in loop_modules.TREES for r in loop_modules.ROOTS))
        self.assertEqual(loop_modules.include_roots([])[:len(loop_modules.ROOTS)], loop_modules.ROOTS)

    def test_include_of_cxx_file(self):
        """A module's include/ a root where it holds a file of a C++ suffix, of every one, and no other file."""
        home, include = "foundation/core/m/module.json", "foundation/core/m/include"
        for suffix in loop_modules.CXX_SUFFIXES:
            self.assertEqual(loop_modules.include_roots([home, f"{include}/x{suffix}"]), (*loop_modules.ROOTS, include))
        self.assertEqual(loop_modules.include_roots([home, f"{include}/README.md"]), loop_modules.ROOTS)


class Chosen(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_trees_choose_structure(self):
        """On the checkout's table, a change to tools/trees.json: every
        structure test, the tooling's, and no other module's; the same
        structure tests left out of a change elsewhere in tools/."""
        table = SELECT["MODULES"]["load"]()
        rows = {m["name"]: m for m in table["modules"]}
        names = {m["name"] for m in table["modules"]}
        tests = [("tests.structure.test_layers.Subset.test_scope", ["fast", "structure"]),
                 ("tests.structure.test_log_includes.Plant.test_k15_caught", ["fast", "structure"]),
                 ("tests.structure.test_boundaries.Plant.test_b17", ["fast", "structure"]),
                 ("tests.drive.test_loop_trees.Roots.test_included", ["drive", "tooling"]),
                 ("tests.drive.test_free.Free.test_held", ["drive", "memory"])]
        why, every = SELECT["choose"](table, rows, ["tools/trees.json"])
        self.assertIsNone(every)
        self.assertEqual(why["tests.structure.*"], "reading tools/trees.json")
        chosen, reasons = SELECT["picked"](tests, why, names)
        self.assertEqual(chosen, [t for t, _ in tests[:4]])
        self.assertIn("3 tests reading tools/trees.json", reasons)
        why, _ = SELECT["choose"](table, rows, ["tools/watch.py"])
        self.assertEqual(SELECT["picked"](tests, why, names)[0], [tests[3][0]])

    def test_lexer_chooses_its_readers(self):
        """On the checkout's table, a change to the structure checks' lexer
        alone: every structure test and the build's tests of the header
        units, which read it, and no other module's."""
        table = SELECT["MODULES"]["load"]()
        rows = {m["name"]: m for m in table["modules"]}
        names = {m["name"] for m in table["modules"]}
        tests = [("tests.structure.test_log_includes.Plant.test_k15_caught", ["fast", "structure"]),
                 ("tests.drive.test_trees_build.TreesBuild.test_header_units", ["fake", "build"]),
                 ("tests.drive.test_loop_trees.Roots.test_included", ["drive", "tooling"]),
                 ("tests.drive.test_free.Free.test_held", ["drive", "memory"])]
        why, every = SELECT["choose"](table, rows, ["tests/structure/lexer.py"])
        self.assertIsNone(every)
        chosen, reasons = SELECT["picked"](tests, why, names)
        self.assertEqual(chosen, [t for t, _ in tests[:2]])
        self.assertIn("2 tests reading tests/structure/lexer.py", reasons)


if __name__ == "__main__":
    unittest.main()
