"""The modules' check (tools/loop_modules.py) on a synthetic table and tree:
a clean one passes, and each plant is caught, its line naming the file or
the edge: a file in no module, a file in two, an undeclared include, an
include resolving to no tracked file (counted, not refused, under
third_party/), an undeclared link, a library reaching past its library's
dependencies, a cycle in depends, a foundation module depending on a
product one outside the table's exceptions; a module directory's include/
an include root, an angle qiven/ include read as a quoted one. The table
read from a synthetic tree of table files (a built file with two rows, one
with from, a slot, a group): its rows, and each refusal of the files on one
line: a row's path holding no file or given twice, a layer or a name not
its directory's, a field of a wrong type or an unknown value, a contract
naming no tracked file, a README.md line differing from its module.json or
naming the consumers. The real table is read from the checkout's tree and
checked for its own errors."""

import contextlib
import copy
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from tools import loop_modules


def table():
    return {
        "exceptions": [{"from": "low", "to": ["high"], "until": "a split"}],
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
        "products/high/module.json": built_file("high", "products", rows[3:]),
        "tools/module.json": built_file("tools", "tools", rows[:1]),
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

    def test_foundation_on_product_outside_the_exception(self):
        t = table()
        t["modules"][1]["test_depends"].append("high")
        self.caught(errors(t), "edge core -> high: foundation depending on product")

    def test_the_exception_listed(self):
        t = table()
        t["exceptions"] = []
        self.caught(errors(t), "edge low -> high: foundation depending on product")

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


class Tree(unittest.TestCase):
    """The table read from a tree's table files, and their refusals."""
    ctest_labels = ("fake",)

    def caught(self, objects, expected, files=None):
        """The plant's line, logged, and no other."""
        found = from_tree(objects, files)[1]
        print(f"plant caught: {found}", file=sys.stderr)
        self.assertEqual(found, [expected])

    def test_rows(self):
        """Each built file's rows, its own two files on its first row or on
        the first its from names; a slot's directory, a group's two files;
        the tree, its table files in it, passes the check."""
        found, errors_ = from_tree()
        self.assertEqual(errors_, [])
        paths = {m["name"]: m["paths"] for m in found["modules"]}
        self.assertEqual([m["name"] for m in found["modules"]],
                         ["core", "low", "foundation", "ai", "notes", "high", "shared"])
        self.assertEqual(paths["core"], ["src/core/", *(f"foundation/core/low/{n}" for n in ("README.md", "module.json"))])
        self.assertEqual(paths["low"], ["src/low/", "tests/unit/low_*",
                                        *(f"foundation/core/time/{n}" for n in ("README.md", "module.json"))])
        self.assertEqual(paths["ai"], ["kits/ai/"])
        self.assertEqual(paths["foundation"], ["foundation/README.md", "foundation/module.json"])
        self.assertEqual(paths["notes"], ["README.md", "module.json"])
        self.assertEqual(paths["shared"], ["tests/runner/", "tools/README.md", "tools/module.json"])
        self.assertEqual(found["modules"][3]["layer"], "no-test")
        own = [p for path in layout() for p in (path.replace("module.json", "README.md"), path)]
        files = {**FILES, **dict.fromkeys(own, b"")}  # the root's README.md the notes'
        self.assertEqual(loop_modules.check({**found, "exceptions": table()["exceptions"]}, sorted(files),
                                            files.__getitem__, TARGETS), [])

    def test_read_from_the_tree(self):
        """tree() reads the table's files under a root, no other module.json."""
        top = Path(tempfile.mkdtemp(prefix="qiven3-loop-modules-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        for path, obj in {**layout(), "other/module.json": {"name": "other"}}.items():
            (top / path).parent.mkdir(parents=True, exist_ok=True)
            (top / path).write_text(json.dumps(obj), encoding="utf-8")
        self.assertEqual(loop_modules.table_files(top), sorted(layout()))
        self.assertEqual(loop_modules.tree(top), from_tree())
        self.assertEqual(loop_modules.load(top), from_tree()[0])
        for path, held in (("module.json", True), ("tools/module.json", True), ("kits/ai/module.json", True),
                           ("other/module.json", False), ("kits/ai/README.md", False), ("tools/x/module.json", False)):
            self.assertEqual(loop_modules.is_table_file(path), held, path)

    def test_does_not_parse(self):
        """Not JSON, or not an object; load() refuses the table, naming it."""
        found = from_tree({**layout(), "products/high/module.json": "{"})[1]
        self.assertEqual(len(found), 1, found)
        self.assertTrue(found[0].startswith("table file products/high/module.json: does not parse: "), found)
        self.caught({**layout(), "products/high/module.json": "[]"},
                    "table file products/high/module.json: is not a JSON object")
        top = Path(tempfile.mkdtemp(prefix="qiven3-loop-modules-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        (top / "products/high").mkdir(parents=True)
        (top / "products/high/module.json").write_text("{", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "^the table: table file products/high/module.json: does not parse"):
            loop_modules.load(top)

    def test_keys(self):
        """A key lacking or added for its status; from with rows; none without."""
        objects = layout()
        del objects["kits/ai/module.json"]["kind"]
        self.caught(objects, "table file kits/ai/module.json: lacks kind")
        objects = layout()
        objects["foundation/module.json"]["present"] = []
        self.caught(objects, "table file foundation/module.json: adds present")
        objects = layout()
        objects["products/high/module.json"]["from"] = ["low"]
        self.caught(objects, "table file products/high/module.json: adds from")
        objects = layout()
        del objects["foundation/core/time/module.json"]["from"]
        self.caught(objects, "table file foundation/core/time/module.json: lacks from")
        objects = layout()
        objects["products/high/module.json"]["present"][0]["tests"] = []
        self.caught(objects, "table file products/high/module.json: a present row lacks or adds a key of name, layer, "
                             "depends, test_depends, paths")
        objects = layout()
        objects["kits/ai/module.json"]["of"] = "core"  # an adapter's
        self.assertEqual(from_tree(objects)[1], [])

    def test_layer_and_status(self):
        objects = layout()
        objects["foundation/kit/x/module.json"] = group_file("x", "kit")
        self.caught(objects, "table file foundation/kit/x/module.json: layer kit is none of "
                    + ", ".join(loop_modules.FILE_LAYERS))
        objects = layout()
        objects["kits/ai/module.json"]["status"] = "planned"
        self.caught(objects, "table file kits/ai/module.json: status planned is none of built, slot, group")

    def test_names(self):
        """Two table files of one name; two rows of one name."""
        objects = layout()
        objects["tools/module.json"]["name"] = "time"  # tools/'s name as it is
        self.caught(objects, "table file tools/module.json: name time is foundation/core/time/module.json's too")
        objects = layout()
        objects["products/high/module.json"]["present"][0]["name"] = "low"
        self.caught(objects, "row low: in foundation/core/low/module.json and products/high/module.json")

    def test_from_naming_no_row(self):
        objects = layout()
        objects["foundation/core/time/module.json"]["from"] = ["gone"]
        self.caught(objects, "table file foundation/core/time/module.json: from gone names no row")
        objects["foundation/core/time/module.json"]["from"] = "low"
        self.caught(objects, "table file foundation/core/time/module.json: from 'low' is not a list of strings")

    def test_slot_and_group_directories(self):
        """A slot's directory holds its two files alone, a group's directly."""
        self.assertEqual(from_tree(files=tracked())[1], [])
        self.caught(None, "slot ai: its directory holds kits/ai/src/ai.cpp", [*tracked(), "kits/ai/src/ai.cpp"])
        self.caught(None, "group foundation: its directory holds foundation/notes.txt",
                    [*tracked(), "foundation/notes.txt"])

    def test_row_paths(self):
        """A row's path holding no tracked file; one path twice in a row."""
        objects = layout()
        objects["products/high/module.json"]["present"][0]["paths"].append("src/gone/")
        self.caught(objects, "row high: path src/gone/ holds no tracked file", tracked())
        objects["products/high/module.json"]["present"][0]["paths"][-1] = "src/high/"
        self.caught(objects, "row high: path src/high/ twice")

    def test_layer_and_name_of_the_directory(self):
        """A layer or a name not its directory's; the deeper of two
        directories of one name prefixed by its parent's name."""
        for path, key, value, expected in (
                ("foundation/core/time/module.json", "layer", "products", "layer products is not its directory's, core"),
                ("products/high/module.json", "layer", "core", "layer core is not its directory's, products"),
                ("kits/ai/module.json", "layer", "products", "layer products is not its directory's, kits"),
                ("foundation/module.json", "layer", "core", "layer core is not its directory's, foundation"),
                ("kits/ai/module.json", "name", "aa", "name aa is not its directory's, ai"),
                ("products/high/x/low/module.json", "name", "low", "name low is not its directory's, x_low")):
            objects = {**layout(), "products/high/x/low/module.json": slot_file("x_low", "products")}
            self.assertEqual(from_tree(objects)[1], [])
            objects[path][key] = value
            self.caught(objects, f"table file {path}: {expected}")

    def test_contract(self):
        """A built file's contract: a tracked file or null, else refused."""
        objects = layout()
        objects["products/high/module.json"]["contract"] = "src/high/high.hpp"
        self.assertEqual(from_tree(objects, tracked())[1], [])
        objects["products/high/module.json"]["contract"] = "src/high/gone.hpp"
        self.caught(objects, "table file products/high/module.json: contract src/high/gone.hpp names no tracked file",
                    tracked())
        objects["products/high/module.json"]["contract"] = ["src/high/high.hpp"]
        self.caught(objects, "table file products/high/module.json: contract ['src/high/high.hpp'] is not a string "
                             "or null")

    def test_readme(self):
        """Each README.md's repeated fields equal its module.json's, one line
        each refused that differs; a line naming the consumers refused."""
        objects = layout()
        objects["products/high/module.json"].update(contract="src/high/high.hpp", parameters=[
            {"name": "bounds", "default": 1, "built": True}, {"name": "leaf", "default": "x", "built": False}])
        texts = {p: json.dumps(o) for p, o in objects.items()}
        good = {"products/high/README.md": "# high\n\nlayer products, status built\n\nWords.\n- **Contract:** "
                                           "`src/high/high.hpp`\n- **Parameters:** `bounds`; `leaf` (not built yet)\n",
                "kits/ai/README.md": "# ai\n\nlayer kits, status slot\n\n- **May depend on:** core\n- **Parameters:** "
                                     "none\n",
                "foundation/README.md": "# foundation\n\nlayer foundation, status group\n"}
        self.assertEqual(loop_modules.table_of(texts, None, good)[1], [])
        for doc, old, new, expected in (
                ("foundation/README.md", "# foundation", "# base", "line 1 is not '# foundation'"),
                ("kits/ai/README.md", "status slot", "status built", "line 3 is not 'layer kits, status slot'"),
                ("products/high/README.md", "`src/high/high.hpp`", "`src/high/other.hpp`",
                 "its Contract line is not '- **Contract:** `src/high/high.hpp`'"),
                ("products/high/README.md", "- **Contract:** `src/high/high.hpp`\n", "",
                 "its Contract line is not '- **Contract:** `src/high/high.hpp`'"),
                ("products/high/README.md", " (not built yet)", "",
                 "its Parameters line is not '- **Parameters:** `bounds`; `leaf` (not built yet)'"),
                ("kits/ai/README.md", "core", "core, system",
                 "its May depend on line is not '- **May depend on:** core'"),
                ("kits/ai/README.md", "none\n", "none\n- **Expected consumers:** high\n",
                 "names its consumers: - **Expected consumers:** high")):
            with self.subTest(expected=expected):
                found = loop_modules.table_of(texts, None, {**good, doc: good[doc].replace(old, new, 1)})[1]
                print(f"plant caught: {found}", file=sys.stderr)
                self.assertEqual(found, [f"readme {doc}: {expected}"])

    def test_types_and_values(self):
        """Each field of a wrong type or an unknown value, refused on its line, never read letter by letter."""
        for path, change, expected in (
                ("kits/ai/module.json", {"name": ["ai"]}, "name ['ai'] is not a string"),
                ("kits/ai/module.json", {"status": ["slot"]}, "status ['slot'] is none of built, slot, group"),
                ("kits/ai/module.json", {"platforms": ["beos"]}, "platforms ['beos'] is not a list of windows"),
                ("kits/ai/module.json", {"platforms": "windows"}, "platforms 'windows' is not a list of windows"),
                ("kits/ai/module.json", {"kind": "plugin"}, "kind 'plugin' is not one of adapter, module"),
                ("kits/ai/module.json", {"may_depend": "core"},
                 "may_depend 'core' is not a list of devkit, core, system, services, kits"),
                ("kits/ai/module.json", {"of": ["core"]}, "of ['core'] is not a string"),
                ("kits/ai/module.json", {"of": "gone"}, "of gone names no row"),
                ("kits/ai/module.json", {"parameters": [{"name": "x", "default": 1}]},
                 "parameters are not a list of objects of keys name, default, built, its name a string, built true "
                 "or false"),
                ("products/high/module.json", {"present": {}}, "present {} is not a list"),
                ("products/high/module.json", {"present": [dict(table()["modules"][3], depends="low")]},
                 "present row 0: depends 'low' is not a list of strings"),
                ("products/high/module.json", {"present": [dict(table()["modules"][3], test_depends="low")]},
                 "present row 0: test_depends 'low' is not a list of strings")):
            objects = layout()
            objects[path].update(change)
            with self.subTest(change=change):
                self.caught(objects, f"table file {path}: {expected}")


if __name__ == "__main__":
    unittest.main()
