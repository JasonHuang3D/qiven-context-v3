"""Every table file under the layer rule (tools/loop_table.py and
tools/loop_modules.py), on the synthetic table and tree: the root
module.json's rows the repository's own at its root, their edges checked;
a row's paths in the old places those its table file gave it at the base,
or fewer; a module of test code alone read as test code, its edges
onto the test kit test edges."""

import json
import sys
import unittest

from tests.drive.test_loop_modules import FILES, TARGETS, built_file, errors, from_tree, layout, table, with_row
from tools import loop_modules

FAULTS = "foundation/devkit/test/faults/module.json"  # a synthetic kit row's home, of a layer not support
LOW = "foundation/core/low/module.json"  # the layout's file of two rows, core first, both of old paths


def old_rows(objects):
    """The base's rows of a tree's table files, {path: object}."""
    return loop_modules.old_rows({p: json.dumps(o) for p, o in objects.items()})


def held(objects, base=None):
    """table_of()'s errors on objects, the layout's old_rows() the base, or base."""
    return loop_modules.table_of({p: json.dumps(o) for p, o in objects.items()}, None, None,
                                 old_rows(layout()) if base is None else base)[1]


def moved(row, path, home):
    """The check's line for a row's path in the old places not given it at the base."""
    return f"row {row}: path {path} in the old places, in {home}, not given it there at the base"


class Rows(unittest.TestCase):
    ctest_labels = ("fake",)

    def caught(self, found, expected):
        """The plant's lines, logged, and no other."""
        print(f"plant caught: {found}", file=sys.stderr)
        self.assertEqual(found, expected)

    def test_the_root_holds_whole_build_rows(self):
        """A root row's path in a tree refused, a root file or directory of no tree not."""
        objects = layout()
        notes = objects["module.json"]["present"][0]
        notes["paths"] += [".github/", "LICENSE"]
        self.caught(from_tree(objects)[1], [])
        notes["paths"] += ["src/px/", "products/px/", "tools/x.py"]
        self.caught(from_tree(objects)[1], [
            f"table file module.json: row notes: path {p} is not the repository's own, at its root, as a "
            "whole-build row's" for p in ("src/px/", "products/px/", "tools/x.py")])

    def test_the_root_rows_edges(self):
        """A root row's edge onto any row of the link graph refused, as an edge up; onto tools/ too."""
        for target, why in (("high", "up from repository to products"), ("low", "up from repository to core")):
            t = with_row(table(), "px", "module.json", [target])
            self.caught(loop_modules.check_table(t), [f"edge px -> {target}: {why}"])
        t = with_row(with_row(table(), "tool", "tools/module.json"), "px", "module.json", ["tool"])
        self.caught(loop_modules.check_table(t), ["edge px -> tool: tool is outside the link graph"])
        t = with_row(table(), "px", "module.json")
        t["modules"][-1]["test_depends"] = ["high"]
        self.caught(loop_modules.check_table(t), ["edge px -> high: up from repository to products"])

    def test_a_row_in_old_places_moved(self):
        """core, its paths in src/: moved to another file, or a new row of old paths first in its own, refused
        by the check; low first in its file, or core's paths moved into high's directory with it, not."""
        self.caught(held(layout()), [])
        objects = layout()
        objects["products/high/module.json"]["present"].append(objects[LOW]["present"].pop(0))
        self.caught(held(objects), [moved("core", "src/core/", "products/high/module.json")])
        objects["products/high/module.json"]["present"][-1]["paths"] = ["products/high/core/"]
        self.caught(held(objects), [])
        objects = layout()
        objects[LOW]["present"].reverse()
        self.caught(held(objects), [])
        objects[LOW]["present"].insert(0, dict(objects[LOW]["present"][0], name="pg", paths=["src/pg/"]))
        self.caught(held(objects), [moved("pg", "src/pg/", LOW)])

    def test_old_places_frozen(self):
        """A row's old paths those its file gave it at the base, or fewer: a new one refused, in an old row or
        a new module's file; one narrowed or dropped, or the row deleted, not; no base, no rule."""
        objects = layout()
        objects[LOW]["present"][0]["paths"] += ["src/grow/", "src/core/deeper/"]
        self.caught(held(objects), [moved("core", "src/grow/", LOW)])
        self.caught(from_tree(objects)[1], [])
        objects = layout()
        objects[LOW]["present"][1]["paths"] = ["src/low/x/", "tests/unit/low_one.cpp"]
        self.caught(held(objects), [])
        del objects[LOW]["present"][0]
        self.caught(held(objects), [])
        objects["foundation/core/pnew/module.json"] = built_file("pnew", "core", [
            {"name": "pnew", "layer": "foundation", "depends": [], "test_depends": [], "paths": ["src/pnew/"]}])
        self.caught(held(objects), [moved("pnew", "src/pnew/", "foundation/core/pnew/module.json")])

    def test_the_tools_and_tests_rows_held(self):
        """The tooling's tests/ path held by its file; a tools/ path new to it not old, another file's
        refused; the rule the checkout's own, its file needed among the tracked files."""
        objects = layout()
        objects["tools/module.json"]["present"][0]["paths"] += ["tests/drive/test_x.py"]
        base = old_rows(objects)
        self.assertEqual(base["tools/module.json"], {"tooling": ["tests/drive/test_x.py"]})
        objects["tools/module.json"]["present"][0]["paths"] += ["tools/y.py", "tests/drive/test_y.py"]
        objects["products/high/module.json"]["present"][0]["paths"] += ["tools/z.py"]
        self.caught(held(objects, base), [moved("high", "tools/z.py", "products/high/module.json"),
                                          moved("tooling", "tests/drive/test_y.py", "tools/module.json")])
        table_, errors_ = loop_modules.tree(files=[])
        self.assertEqual(errors_[0], f"{loop_modules.BASE}: not a tracked file: the old places unchecked")
        real = {p: (loop_modules.ROOT / p).read_text(encoding="utf-8") for p in loop_modules.table_files(
            loop_modules.ROOT)}
        rows = json.loads(real["foundation/core/memory/module.json"])
        rows["present"][0]["paths"].append("src/pgrow/")
        found = loop_modules.table_of({**real, "foundation/core/memory/module.json": json.dumps(rows)}, None, None,
                                      json.loads((loop_modules.ROOT / loop_modules.BASE).read_text("utf-8"))["rows"])
        self.caught(found[1], [moved("memory", "src/pgrow/", "foundation/core/memory/module.json")])

    def test_test_code_alone_reaches_the_kit_by_test_edges(self):
        """A module of test code alone: its include and link onto a kit row accepted by test_depends, its
        depends edge onto the kit refused, with no exception."""
        files = {**FILES, "tests/faults/faults.hpp": b"#pragma once\n",
                 "tests/probe/probe_test.cpp": b'#include "faults/faults.hpp"\n'}
        targets = [*TARGETS, ("faults_lib", ["tests/faults/faults.hpp"], []),
                   ("probe_cases", ["tests/probe/probe_test.cpp"], ["faults_lib"])]
        t = with_row(with_row(table(), "faults", FAULTS), "probe", "products/high/module.json")
        t["modules"][-2]["paths"] = ["tests/faults/"]
        t["modules"][-1].update(paths=["tests/probe/"], test_depends=["faults"])
        self.caught(errors(t, files, targets), [])
        t["modules"][-1].update(depends=["faults"], test_depends=[])
        t["exceptions"].append({"from": "probe", "to": ["faults"], "until": "never"})
        self.caught(loop_modules.check_table(t), [
            "edge probe -> faults: a library on the test kit from test code alone, whose edge onto it is a test edge"])

    def test_the_real_rows(self):
        """The checkout's child and structure, test code alone, on faults by test edges."""
        table_ = loop_modules.load()
        rows = {m["name"]: m for m in table_["modules"]}
        for name in ("child", "structure"):
            self.assertIn("faults", rows[name]["test_depends"])
            self.assertNotIn("faults", rows[name]["depends"])


if __name__ == "__main__":
    unittest.main()
