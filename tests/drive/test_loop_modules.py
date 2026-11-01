"""The modules' check (tools/loop_modules.py) on a synthetic table and tree:
a clean one passes, and each plant is caught, its line naming the file or
the edge: a file in no module, a file in two, an undeclared include, an
include resolving to no tracked file (counted, not refused, under
third_party/), an undeclared link, a library reaching past its library's
dependencies, a cycle in depends, an edge up by the layers of the rows'
table files outside the test kit's reach and the table's exceptions, an
exception naming no edge or one the rule allows, the support layer outside
the test kit, an edge onto tools/, a library file, target or edge reaching
the test kit, which test code alone reaches, library code including or
linking test code, a module directory's tests/ test code; a module
directory's include/
an include root, an angle qiven/ include read as a quoted one. The real
table is read from the checkout's tree and checked for its own errors; the
trees from tools/trees.json, as the root CMakeLists.txt reads them. The
table read from a synthetic tree of table files is
tests/drive/test_loop_table.py's, on this file's table and layout."""

import contextlib
import copy
import io
import json
import sys
import unittest

from tools import loop_modules


def table():
    return {
        "exceptions": [{"from": "low", "to": ["high"], "until": "a split"}],
        "homes": {"shared": "foundation/devkit/test/shared/module.json", "core": "foundation/core/low/module.json",
                  "low": "foundation/core/low/module.json", "high": "products/high/module.json"},
        "modules": [
            {"name": "shared", "layer": "support", "depends": ["core"], "test_depends": [], "paths": ["tests/runner/"]},
            {"name": "core", "layer": "foundation", "depends": [], "test_depends": [], "paths": ["src/core/"]},
            {"name": "low", "layer": "foundation", "depends": ["core"], "test_depends": ["high"],
             "paths": ["src/low/", "tests/unit/low_*"]},
            {"name": "high", "layer": "product", "depends": ["low"], "test_depends": [],
             "paths": ["src/high/", "tests/unit/high_*"]},
        ],
    }


FILES = {
    "src/core/core.hpp": b"#pragma once\n",
    "src/low/low.cpp": b'#include "core/core.hpp"\n',
    "src/high/high.hpp": b'#include "low/low.hpp"\n#include <vector>\n',
    "src/low/low.hpp": b'#include "core/core.hpp"\n',
    "tests/runner/runner.cpp": b'#include "core/core.hpp"\n#include "runner.hpp"\n',
    "tests/runner/runner.hpp": b"#pragma once\n",
    "tests/unit/high_test.cpp": b'#include "high/high.hpp"\n#include "runner/runner.hpp"\n',
    "tests/unit/low_test.cpp": b'#include "low/low.hpp"\n#include "high/high.hpp"\n',
}
TARGETS = [
    ("core_lib", ["src/core/core.hpp"], []),
    ("low_lib", ["src/low/low.cpp"], ["core_lib"]),
    ("high_lib", ["src/high/high.hpp"], ["low_lib", "core_lib"]),
    ("runner_lib", ["tests/runner/runner.cpp"], ["core_lib"]),
    ("high_cases", ["tests/unit/high_test.cpp"], ["high_lib", "low_lib", "core_lib", "runner_lib"]),
    ("low_cases", ["tests/unit/low_test.cpp"], ["low_lib", "high_lib", "runner_lib"]),
]


KIT = "foundation/devkit/test/shared/"  # the synthetic test kit's directory


def with_row(table_, name, home, depends=()):
    """table_ and a row of no paths, its home's layer the tree's."""
    table_["modules"].append({"name": name, "layer": "foundation", "depends": list(depends), "test_depends": [],
                              "paths": []})
    table_["homes"][name] = home
    return table_


def errors(table_=None, files=None, targets=TARGETS):
    files = FILES if files is None else files
    return loop_modules.check(table_ or table(), sorted(files), files.__getitem__, targets)


def built_file(name, layer, rows, **more):
    """A built module's module.json: its present rows, or none and from."""
    return {"name": name, "layer": layer, "status": "built", "platforms": ["windows"], "contract": None,
            "adopted": "2026-10-31", "parameters": [], "present": copy.deepcopy(rows), **more}


def slot_file(name, layer):
    return {"name": name, "layer": layer, "status": "slot", "kind": "module", "may_depend": ["core"],
            "platforms": [], "adopted": "2026-10-31", "parameters": []}


def group_file(name, layer):
    return {"name": name, "layer": layer, "status": "group", "adopted": "2026-10-31"}


def layout():
    """table()'s rows as a tree's table files, {path: its object}: a built
    file with two rows, one with no rows and from, a slot, a group."""
    rows = table()["modules"]
    return {
        "module.json": built_file("repository", "repository", [
            {"name": "notes", "layer": "no-test", "depends": [], "test_depends": [], "paths": ["README.md"]}]),
        "foundation/module.json": group_file("foundation", "foundation"),
        "foundation/core/low/module.json": built_file("low", "core", rows[1:3]),
        "foundation/core/time/module.json": built_file("time", "core", [], **{"from": ["low"]}),
        "kits/ai/module.json": slot_file("ai", "kits"),
        "foundation/devkit/test/shared/module.json": built_file("shared", "devkit", rows[:1]),
        "products/high/module.json": built_file("high", "products", rows[3:]),
        "tools/module.json": built_file("tools", "tools", [
            {"name": "tooling", "layer": "tooling", "depends": [], "test_depends": [], "paths": ["tools/"]}]),
    }


def tracked():
    """FILES and the layout's table files, each with its README.md."""
    return sorted({*FILES, *(p for path in layout() for p in (path.replace("module.json", "README.md"), path))})


def from_tree(objects=None, files=None):
    """table_of() of the layout, or of objects, given as text."""
    objects = layout() if objects is None else objects
    return loop_modules.table_of({p: o if isinstance(o, str) else json.dumps(o) for p, o in objects.items()}, files)


class Check(unittest.TestCase):
    ctest_labels = ("fake",)

    def caught(self, found, expected):
        """The plant's line, logged, and no other."""
        print(f"plant caught: {found}", file=sys.stderr)
        self.assertEqual(found, [expected])

    def test_clean(self):
        self.assertEqual(errors(), [])

    def test_file_in_no_module(self):
        self.caught(errors(files={**FILES, "src/stray.cpp": b""}), "file src/stray.cpp: in no module")

    def test_file_in_two(self):
        t = table()
        t["modules"][1]["paths"].append("src/low/low.hpp")
        self.caught(errors(t), "file src/low/low.hpp: in modules core, low")

    def test_undeclared_include(self):
        files = {**FILES, "src/core/core.hpp": b'#include "low/low.hpp"\n'}
        self.caught(errors(files=files), "include src/core/core.hpp -> src/low/low.hpp: module core does not depend on low")

    def test_unresolved_include(self):
        files = {**FILES, "src/core/core.hpp": b'#pragma once\n#include "gone.hpp"\n'}
        self.caught(errors(files=files), 'include src/core/core.hpp:2 "gone.hpp": resolves to no tracked file')

    def test_vendored_include_counted(self):
        """Under third_party/ an unresolved include is counted, not refused."""
        t = table()
        t["modules"].append({"name": "vendor", "layer": "foundation", "depends": [], "paths": ["third_party/"]})
        files = {**FILES, "third_party/lib/lib.c": b'#include "lib.h"\n#include "config.h"\n', "third_party/lib/lib.h": b""}
        left = []
        self.assertEqual(loop_modules.check(t, sorted(files), files.__getitem__, TARGETS, left), [])
        self.assertEqual(left, ['third_party/lib/lib.c:2 "config.h"'])

    def test_parent_steps_resolved(self):
        """An include's .. steps taken from the including file's directory."""
        self.assertEqual(errors(files={**FILES, "src/low/low.cpp": b'#include "../core/core.hpp"\n'}), [])
        files = {**FILES, "src/core/core.hpp": b'#include "../low/low.hpp"\n'}
        self.caught(errors(files=files), "include src/core/core.hpp -> src/low/low.hpp: module core does not depend on low")

    def test_undeclared_link(self):
        targets = [*TARGETS[:4], ("core_cases", ["src/core/core.hpp"], ["low_lib"])]
        self.caught(errors(targets=targets), "link core_cases -> low_lib: module core does not depend on low")

    def test_support_reaches_its_own_dependencies_alone(self):
        targets = [*TARGETS[:3], ("runner_lib", ["tests/runner/runner.cpp"], ["core_lib", "low_lib"])]
        self.caught(errors(targets=targets), "link runner_lib -> low_lib: module shared does not depend on low")

    def test_library_reaches_its_library_dependencies_alone(self):
        """A library file's include and a library target's link reach by
        depends; a test file's and a test target's by test_depends too; a
        module whose every file is under tests/ is its own library."""
        t = table()
        t["modules"][3].update(depends=[], test_depends=["low"])
        targets = [*TARGETS[:2], ("high_lib", ["src/high/high.hpp"], []), *TARGETS[3:]]
        files = {**FILES, "src/high/high.hpp": b"#pragma once\n", "tests/unit/high_test.cpp": b'#include "low/low.hpp"\n'}
        self.assertEqual(errors(t, files, targets), [])
        self.caught(errors(t, targets=targets), "include src/high/high.hpp -> src/low/low.hpp: module high does not "
                                                "depend on low")
        linked = [*TARGETS[:2], ("high_lib", ["src/high/high.hpp"], ["low_lib"]), *TARGETS[3:]]
        self.caught(errors(t, files, linked), "link high_lib -> low_lib: module high does not depend on low")
        t["modules"].append({"name": "kit", "layer": "foundation", "depends": [], "test_depends": ["core"],
                             "paths": ["tests/kit/"]})
        self.assertEqual(errors(t, {**files, "tests/kit/kit.cpp": b'#include "core/core.hpp"\n'}, targets), [])
        t["modules"][-1].update(test_depends=[])  # test code alone: its tests' edges its own
        self.caught(errors(t, {**files, "tests/kit/kit.cpp": b'#include "core/core.hpp"\n'}, targets),
                    "include tests/kit/kit.cpp -> src/core/core.hpp: module kit does not depend on core")

    def test_cycle_in_depends(self):
        """A cycle in depends refused, its modules in order; in test_depends, not."""
        t = table()
        t["modules"][2]["depends"].append("high")
        self.caught(loop_modules.check_table(t), "a cycle in depends: high -> low -> high")

    def test_include_directory_of_a_module(self):
        """A module directory's include/ under foundation/, kits/ or products/ an include root."""
        files = {**FILES, "foundation/core/low/module.json": b"{}", "foundation/core/low/include/low/api.hpp": b"",
                 "src/low/low.cpp": b'#include "low/api.hpp"\n'}
        t = table()
        t["modules"][2]["paths"].append("foundation/core/low/")
        self.assertEqual(errors(t, files), [])
        del files["foundation/core/low/module.json"]
        t["modules"][2]["paths"][-1] = "foundation/core/low/include/"
        self.caught(errors(t, files), 'include src/low/low.cpp:1 "low/api.hpp": resolves to no tracked file')
        self.assertTrue(set(loop_modules.TOPS) <= set(loop_modules.TREES))

    def test_trees_named_once(self):
        """The trees read from tools/trees.json, which the root CMakeLists.txt
        reads for the analysed roots, its layers and analysed, holding no list
        of its own."""
        trees = json.loads((loop_modules.ROOT / "tools" / "trees.json").read_text(encoding="utf-8"))
        self.assertEqual(sorted(trees), ["analysed", "included", "layers", "walked"])
        self.assertEqual(loop_modules.TOPS, tuple(trees["layers"]))
        self.assertEqual(loop_modules.TREES, (*trees["walked"], *trees["layers"]))
        self.assertTrue(all(a.split("/", 1)[0] in loop_modules.TREES for a in trees["analysed"]))
        cmake = (loop_modules.ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('"${PROJECT_SOURCE_DIR}/tools/trees.json"', cmake)
        self.assertIn("set(qiven3_roots ${qiven3_layers} ${qiven3_analysed})", cmake)
        self.assertNotIn("tests/support|", cmake)

    def test_angle_includes_read(self):
        """A quoted and an angle include of another module's include/qiven/
        header need the edge; an angle qiven/ include resolving to nothing is
        refused; a standard header is not read."""
        t = table()
        t["modules"].append({"name": "api", "layer": "foundation", "depends": [], "test_depends": [],
                             "paths": ["foundation/core/api/"]})
        api = {"foundation/core/api/module.json": b"{}", "foundation/core/api/include/qiven/api/api.hpp": b""}
        for line in (b'#include "qiven/api/api.hpp"\n', b"#  include <qiven/api/api.hpp>\n"):
            files = {**FILES, **api, "src/core/core.hpp": line + b"#include <vector>\n"}
            with self.subTest(line=line):
                self.caught(errors(t, files), "include src/core/core.hpp -> foundation/core/api/include/qiven/api/"
                                              "api.hpp: module core does not depend on api")
                declared = copy.deepcopy(t)
                declared["modules"][1]["depends"].append("api")
                self.assertEqual(errors(declared, files), [])
        files = {**FILES, "src/core/core.hpp": b"#include <vector>\n#include <qiven/gone.hpp>\n"}
        self.caught(errors(files=files), "include src/core/core.hpp:2 <qiven/gone.hpp>: resolves to no tracked file")

    def test_upward_edge_outside_the_exception(self):
        """An edge up, in test_depends or depends, refused by the layers of
        the rows' table files; down, or within a layer, not."""
        t = table()
        t["modules"][1]["test_depends"].append("high")
        self.caught(errors(t), "edge core -> high: up from core to products")
        t = with_row(table(), "sys", "foundation/system/sys/module.json")
        t["modules"][1]["depends"].append("sys")
        self.caught(loop_modules.check_table(t), "edge core -> sys: up from core to system")
        t["modules"][-1]["depends"].append("core")
        t["modules"][1]["depends"].remove("sys")
        self.assertEqual(loop_modules.check_table(t), [])

    def test_the_test_kits_reach(self):
        """The test kit's edges up to core to services allowed, higher not; another devkit row's not."""
        t = with_row(table(), "sys", "foundation/system/sys/module.json")
        t["modules"][0]["depends"].append("sys")
        self.assertEqual(loop_modules.check_table(t), [])
        t["modules"][0]["depends"].append("high")
        self.caught(loop_modules.check_table(t), "edge shared -> high: up from devkit to products, past the test "
                                                 "kit's reach")
        t = with_row(table(), "tool", "foundation/devkit/structure/module.json", ["core"])
        self.caught(loop_modules.check_table(t), "edge tool -> core: up from devkit to core")

    def test_the_test_kit_from_test_code_alone(self):
        """Test code reaches the test kit's support with no edge, library code
        never; a library's depends onto the kit refused, a test-only module's not."""
        files = {**FILES, "src/core/core.hpp": b'#include "runner/runner.hpp"\n'}
        self.caught(errors(files=files), "include src/core/core.hpp -> tests/runner/runner.hpp: library code "
                                         "including test code")
        linked = [("core_lib", ["src/core/core.hpp"], ["runner_lib"]), *TARGETS[1:]]
        self.caught(errors(targets=linked), "link core_lib -> runner_lib: library code linking test code")
        t = table()  # the kit's library outside tests/
        t["modules"][0]["paths"].append(KIT)
        files = {**FILES, f"{KIT}include/shared/kit.hpp": b"", f"{KIT}module.json": b"{}",
                 "src/core/core.hpp": b'#include "shared/kit.hpp"\n'}
        self.caught(errors(t, files), f"include src/core/core.hpp -> {KIT}include/shared/kit.hpp: module core does "
                                      "not depend on shared")
        t = table()
        t["modules"][2]["depends"].append("shared")
        self.caught(loop_modules.check_table(t), "edge low -> shared: a library on the test kit, which test code "
                                                 "alone reaches")
        t["modules"][2]["depends"].remove("shared")
        t["modules"][2]["test_depends"].append("shared")
        t = with_row(t, "tool", "foundation/devkit/structure/module.json")
        t["modules"][-1].update(paths=["tests/tool/"], test_depends=["shared"])  # test code alone: a test edge
        self.assertEqual(loop_modules.check_table(t), [])

    def test_a_modules_own_tests(self):
        """A module directory's tests/ test code, as the top-level tests/ (the
        support's reaching its test_depends); with no table file there, not."""
        t = table()
        t["modules"][2]["paths"].append("foundation/core/low/")
        t["modules"][0].update(test_depends=["low"], paths=["tests/runner/", KIT])
        moved = {"foundation/core/low/module.json": b"{}", f"{KIT}module.json": b"{}",
                 "foundation/core/low/tests/moved_test.cpp": b'#include "high/high.hpp"\n#include "runner/runner.hpp"',
                 f"{KIT}src/kit.cpp": b'#include "core/core.hpp"\n', f"{KIT}tests/kit_test.cpp": b'#include "low/low.hpp"'}
        self.assertEqual(errors(t, {**FILES, **moved}), [])
        del moved["foundation/core/low/module.json"]
        self.assertEqual(errors(t, {**FILES, **moved}), [
            "include foundation/core/low/tests/moved_test.cpp -> src/high/high.hpp: module low does not depend on high",
            "include foundation/core/low/tests/moved_test.cpp -> tests/runner/runner.hpp: library code including "
            "test code"])
        moved[f"{KIT}src/kit.cpp"] = b'#include "low/low.hpp"\n'
        self.assertIn(f"include {KIT}src/kit.cpp -> src/low/low.hpp: module shared does not depend on low",
                      errors(t, {**FILES, **moved}))

    def test_layer_of_the_file_not_the_word(self):
        """The rule reads a row's file, not its word; the support word outside
        the test kit refused; an edge onto tools/ refused, theirs not ordered."""
        t = table()
        t["exceptions"] = []
        t["modules"][2]["layer"] = "tooling"
        self.caught(loop_modules.check_table(t), "edge low -> high: up from core to products")
        t = table()
        t["modules"][1]["layer"] = "support"
        self.caught(loop_modules.check_table(t), "module core: layer support outside the test kit, "
                                                 "foundation/devkit/test/")
        t = with_row(table(), "tooling", "tools/module.json", ["high"])
        self.assertEqual(loop_modules.check_table(t), [])
        t["modules"][1]["test_depends"].append("tooling")
        self.caught(loop_modules.check_table(t), "edge core -> tooling: tooling is outside the link graph")

    def test_the_exception_listed(self):
        """An exception lifts its edge; one naming no edge of the table, or an
        edge the rule allows, is refused itself."""
        t = table()
        t["exceptions"] = []
        self.caught(errors(t), "edge low -> high: up from core to products")
        t = table()
        t["exceptions"][0]["to"].append("shared")
        self.caught(loop_modules.check_table(t), "exception low -> shared: the table has no such edge")
        t = table()
        t["exceptions"].append({"from": "high", "to": ["low"], "until": "never"})
        self.caught(loop_modules.check_table(t), "exception high -> low: the layer rule allows the edge")

    def test_help_and_usage(self):
        """--help and -h print the usage and exit 0; a wrong count fails, 2."""
        for flag in ("--help", "-h"):
            with self.subTest(flag=flag), contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(loop_modules.main([flag]), 0)
            self.assertIn("loop_modules.py SOURCE [BUILD]", out.getvalue())
        for argv in ([], ["a", "b", "c"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(loop_modules.main(argv), 2)
            self.assertIn("usage:", err.getvalue())

    def test_the_real_table(self):
        """The checkout's table, read from its tree: its files' own check,
        and its layers, names and foundation line."""
        found, errors_ = loop_modules.tree()
        self.assertEqual(errors_, [])
        self.assertEqual(loop_modules.check_table(found), [])
        self.assertEqual(loop_modules.load(), found)
        # The nine edges the layer rule refuses, named exceptions until what removes them; none added.
        self.assertEqual([(e["from"], e["to"]) for e in found["exceptions"]], [
            ("base", ["platform"]), ("structure", ["store", "checked", "write", "location", "app", "driver", "child"]),
            ("diag", ["log"])])


if __name__ == "__main__":
    unittest.main()
