"""The modules' table (tools/loop_modules.py) read from a synthetic tree of
table files (a built file with two rows, one with from, a slot, a group):
its rows, and each refusal of the files on one line: a row's path holding
no file or given twice, a layer or a name not its directory's, a field of
a wrong type or an unknown value, a contract naming no tracked file, a
README.md line differing from its module.json or naming the consumers."""

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive.test_loop_modules import FILES, TARGETS, from_tree, group_file, layout, slot_file, table, tracked
from tools import loop_modules


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
                         ["core", "low", "shared", "foundation", "ai", "notes", "high", "tooling"])
        self.assertEqual(paths["core"], ["src/core/", *(f"foundation/core/low/{n}" for n in ("README.md", "module.json"))])
        self.assertEqual(paths["low"], ["src/low/", "tests/unit/low_*",
                                        *(f"foundation/core/time/{n}" for n in ("README.md", "module.json"))])
        self.assertEqual(paths["ai"], ["kits/ai/"])
        self.assertEqual(paths["foundation"], ["foundation/README.md", "foundation/module.json"])
        self.assertEqual(paths["notes"], ["README.md", "module.json"])
        self.assertEqual(paths["shared"], ["tests/runner/", *(f"foundation/devkit/test/shared/{n}"
                                                              for n in ("README.md", "module.json"))])
        self.assertEqual(paths["tooling"], ["tools/", "tools/README.md", "tools/module.json"])
        self.assertEqual(found["modules"][4]["layer"], "no-test")
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

    def test_exceptions(self):
        """A built file's exceptions, for its own rows alone, read into the
        table; a slot's refused; each row's home its table file."""
        objects = layout()
        objects["foundation/core/low/module.json"]["exceptions"] = [{"from": "low", "to": ["high"], "until": "a split"}]
        found, errors_ = from_tree(objects)
        self.assertEqual(errors_, [])
        self.assertEqual(found["exceptions"], table()["exceptions"])
        self.assertEqual(found["homes"]["low"], "foundation/core/low/module.json")
        self.assertEqual(found["homes"]["shared"], "foundation/devkit/test/shared/module.json")
        objects["foundation/core/low/module.json"]["exceptions"][0]["from"] = "high"
        self.caught(objects, "table file foundation/core/low/module.json: exception from high names no row of its file")
        objects = layout()
        objects["kits/ai/module.json"]["exceptions"] = []
        self.caught(objects, "table file kits/ai/module.json: adds exceptions")

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
                ("products/high/module.json", {"exceptions": [{"from": "high", "to": [], "until": "x"}]},
                 "exceptions are not a list of objects of keys from, to, until, from a string, to a list of strings, "
                 "until words, neither empty"),
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
