"""Every table file under the layer rule (tools/loop_table.py and
tools/loop_modules.py), on the synthetic table and tree: the root
module.json's rows the repository's own at its root, their edges checked;
a row's paths in the old places held in its own table file alone, until
its module moves; a module of test code alone read as test code, its edges
onto the test kit test edges."""

import copy
import sys
import unittest

from tests.drive.test_loop_modules import FILES, TARGETS, errors, from_tree, layout, table, with_row
from tools import loop_modules

FAULTS = "foundation/devkit/test/faults/module.json"  # a synthetic kit row's home, of a layer not support


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
        """core, its paths in src/, the first row of a table file not of its name: moved after another
        file's first row, or behind its own file's next, refused by the check."""
        self.caught(from_tree()[1], [])
        objects = layout()
        objects["products/high/module.json"]["present"].append(objects["foundation/core/low/module.json"][
            "present"].pop(0))
        self.caught(from_tree(objects)[1], [
            "row core: its path src/core/ in the old places, in products/high/module.json, not its own table "
            "file: the one of its name, or else the one whose first row it is"])
        objects = layout()
        low = objects["foundation/core/low/module.json"]["present"]
        low.reverse()  # low, of its file's name, first: core no longer first
        self.caught(from_tree(objects)[1], [
            "row core: its path src/core/ in the old places, in foundation/core/low/module.json, not its own "
            "table file: the one of its name, or else the one whose first row it is"])

    def test_shared_rows_until_they_move(self):
        """A row SHARED names: held in its file while in the old places, refused once it has none there,
        and refused in another file."""
        home = "products/qiven3/tests/module.json"
        driver = {"name": "driver", "layer": "product", "depends": [], "test_depends": [], "paths": ["tests/d/"]}
        child = dict(driver, name="child", paths=["tests/child/"])
        objects = {**layout(), "products/qiven3/module.json": copy.deepcopy(layout()["products/high/module.json"])}
        objects["products/qiven3/module.json"].update(name="qiven3", present=[dict(driver, name="qiven3")])
        objects[home] = dict(objects["products/high/module.json"], name="tests", present=[driver, child])
        self.caught(from_tree(objects)[1], [])
        child["paths"] = ["products/qiven3/tests/child/"]
        self.caught(from_tree(objects)[1], [
            "row child: no path left in the old places: its module moved, SHARED holds it no more"])
        child["paths"] = ["tests/child/"]
        objects["products/qiven3/module.json"]["present"].append(objects[home]["present"].pop())
        self.caught(from_tree(objects)[1], [
            "row child: its path tests/child/ in the old places, in products/qiven3/module.json, not its own "
            "table file: the one of its name, or else the one whose first row it is"])

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
        """The checkout's child and structure, test code alone, on faults by test edges; its SHARED rows its."""
        table_ = loop_modules.load()
        rows = {m["name"]: m for m in table_["modules"]}
        for name in ("child", "structure"):
            self.assertIn("faults", rows[name]["test_depends"])
            self.assertNotIn("faults", rows[name]["depends"])
        shared = {f"{home}/module.json": names for home, names in loop_modules.SHARED.items()}
        self.assertEqual({name: table_["homes"][name] for names in shared.values() for name in names},
                         {name: home for home, names in shared.items() for name in names})


if __name__ == "__main__":
    unittest.main()
