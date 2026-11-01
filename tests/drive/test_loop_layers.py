"""The layer rule of the modules' check (tools/loop_modules.py), closed: a
library's depends reaching the test kit through a module of test code alone
refused, its path on to the kit named, and its includes no longer covered
through it; an INTERFACE target read from the codemodel, a library linking
the kit's refused, the build's own not read; a product on another product
refused; an exception's blank until or its edge listed twice refused, and an
exception never lifting a library's edge onto the kit; a row's path outside
its table file's directory but in today's places refused, and a row outside
the table file of its name. On the real table: its nine exceptions pinned,
and the rows whose paths still lie in today's places, each in its table
file, pinned, so a tenth exception or a row moved to another file fails."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive.test_loop_modules import FILES, KIT, errors, from_tree, layout, table, with_row
from tools import loop_modules


class Rule(unittest.TestCase):
    """The rule on a synthetic table and tree."""
    ctest_labels = ("fake",)

    def caught(self, found, expected):
        """The plant's lines, logged, and no others."""
        print(f"plant caught: {found}", file=sys.stderr)
        self.assertEqual(found, expected)

    def test_a_library_through_test_code_alone(self):
        """A library on a module of test code alone, which reaches the kit, is
        refused, its path named; its include of what the kit alone reaches too."""
        t = with_row(with_row(table(), "svc", "foundation/services/svc/module.json"), "pt",
                     "foundation/core/pt/module.json")
        t["modules"][-2]["paths"], t["modules"][-1]["paths"] = ["src/svc/"], ["foundation/core/pt/tests/"]
        t["modules"][-1]["test_depends"] = ["shared"]  # test code alone: onto the kit by a test edge
        t["modules"][0]["depends"].append("svc")  # the kit's edge up to services, allowed
        t["modules"][2]["depends"].append("pt")
        files = {**FILES, "src/svc/svc.hpp": b"", "foundation/core/pt/tests/pt_test.cpp": b"",
                 "src/low/low.cpp": b'#include "core/core.hpp"\n#include "svc/svc.hpp"\n'}
        self.caught(errors(t, files, []), [
            "edge low -> pt: a library on pt, test code alone, onto the test kit by pt -> shared",
            "include src/low/low.cpp -> src/svc/svc.hpp: module low does not depend on svc"])
        t["modules"][2]["depends"].remove("pt")
        t["modules"][2]["test_depends"].append("pt")  # its tests on it, allowed
        self.caught(errors(t, files, []), ["include src/low/low.cpp -> src/svc/svc.hpp: module low does not "
                                           "depend on svc"])

    def test_interface_targets(self):
        """An INTERFACE target, its sources its CMakeLists.txt's, read with the
        links each target gives; a library linking the kit's refused, the
        build's own target not read, an imported one left out."""
        with tempfile.TemporaryDirectory() as tmp:
            reply = Path(tmp) / ".cmake" / "api" / "v1" / "reply"
            reply.mkdir(parents=True)
            targets = {
                "core_lib": {"type": "STATIC_LIBRARY", "sources": [{"path": "src/core/core.hpp"}],
                             "linkLibraries": [{"id": "kit_if"}, {"id": "flags"}, {"fragment": "kernel32"}]},
                "kit_if": {"type": "INTERFACE_LIBRARY", "paths": {"source": "tests/runner"},
                           "interfaceLinkLibraries": [{"id": "core_lib"}]},
                "flags": {"type": "INTERFACE_LIBRARY", "paths": {"source": "."}},
                "Py::Interpreter": {"type": "EXECUTABLE", "imported": True}}
            for i, (name, data) in enumerate(targets.items()):
                (reply / f"t{i}.json").write_text(json.dumps({"id": name, "name": name, **data}), encoding="utf-8")
            refs = [{"jsonFile": f"t{i}.json"} for i in range(len(targets))]
            (reply / "model.json").write_text(json.dumps({"configurations": [
                {"targets": refs[:1], "abstractTargets": refs[1:]}]}), encoding="utf-8")
            (reply / "index-1.json").write_text(json.dumps({"reply": {"codemodel-v2": {"jsonFile": "model.json"}}}),
                                                encoding="utf-8")
            model = loop_modules.codemodel(tmp, tmp)
        self.assertEqual(model, [("core_lib", ["src/core/core.hpp"], ["kit_if", "flags"]),
                                 ("kit_if", ["tests/runner/CMakeLists.txt"], ["core_lib"]),
                                 ("flags", ["CMakeLists.txt"], [])])
        t = with_row(table(), "build", "foundation/devkit/build/module.json")
        t["modules"][-1].update(layer="whole-build", paths=["CMakeLists.txt"])
        t["modules"][0]["paths"].append(KIT)
        files = {**FILES, "tests/runner/CMakeLists.txt": b"", "CMakeLists.txt": b"", f"{KIT}src/kit.cpp": b""}
        self.caught(errors(t, files, model), ["link core_lib -> kit_if: library code linking test code"])
        model = [("core_lib", ["src/core/core.hpp"], ["kit_lib"]), ("kit_lib", [f"{KIT}src/kit.cpp"], [])]
        self.caught(errors(t, files, model), ["link core_lib -> kit_lib: a library on the test kit, which test "
                                              "code alone reaches"])

    def test_a_product_on_another(self):
        """A product's row on another product's refused, in depends or its
        tests'; on its own product's, allowed."""
        t = with_row(with_row(table(), "gp", "products/gp/module.json", ["high"]), "own",
                     "products/high/own/module.json", ["high"])
        self.caught(loop_modules.check_table(t), ["edge gp -> high: a product on another product, high"])
        t["modules"][-2].update(depends=[], test_depends=["high"])
        self.caught(loop_modules.check_table(t), ["edge gp -> high: a product on another product, high"])

    def test_the_exceptions_nits(self):
        """An exception's edge listed twice refused; one never lifts a library's
        edge onto the kit; a blank until refused."""
        for change in (lambda e: e[0]["to"].append("high"), lambda e: e.append(dict(e[0]))):
            t = table()
            change(t["exceptions"])
            self.caught(loop_modules.check_table(t), ["exception low -> high: listed twice"])
        t = table()
        t["modules"][2]["depends"].append("shared")
        t["exceptions"][0]["to"].append("shared")
        self.caught(loop_modules.check_table(t), ["edge low -> shared: a library on the test kit, which test code "
                                                  "alone reaches"])
        objects = layout()
        objects["products/high/module.json"]["exceptions"] = [{"from": "high", "to": ["low"], "until": " "}]
        self.caught(from_tree(objects)[1], [
            "table file products/high/module.json: exceptions are not a list of objects of keys from, to, until, "
            "from a string, to a list of strings, until words, neither empty"])

    def test_a_row_in_its_directory(self):
        """A row's path outside its table file's directory refused but in
        today's places, a root file a whole-build row's; a row in a table file
        other than the one of its name refused."""
        objects = layout()
        low = objects["foundation/core/low/module.json"]["present"]
        low[0]["paths"] += ["kits/ai/x.hpp", "CMakeLists.txt"]
        self.caught(from_tree(objects)[1], [
            f"table file foundation/core/low/module.json: row core: path {p} lies outside foundation/core/low/"
            for p in ("kits/ai/x.hpp", "CMakeLists.txt")])
        low[0]["paths"].remove("kits/ai/x.hpp")
        low[0]["layer"] = "whole-build"
        self.caught(from_tree(objects)[1], [])
        objects = layout()
        high = objects["products/high/module.json"]
        objects["foundation/core/low/module.json"]["present"] += high["present"]
        high.update(present=[], **{"from": ["low"]})
        self.caught(from_tree(objects)[1], ["row high: in foundation/core/low/module.json, not in "
                                            "products/high/module.json, the table file of its name"])


QIVEN3 = "products/qiven3/"
OUTSIDE = {  # the rows whose paths still lie in today's places, by their table file's directory
    "foundation/core/matcher": ["matcher"], "foundation/core/memory": ["memory"], "foundation/core/text": ["base"],
    "foundation/devkit/build": ["build"], "foundation/devkit/structure": ["structure"],
    "foundation/devkit/test/faults": ["faults"], "foundation/devkit/test/runner": ["runner", "support"],
    "foundation/services/compute": ["hal"], "foundation/services/log": ["log"],
    "foundation/services/storage/recordlog": ["store", "keys"], "foundation/services/transport": ["wire"],
    "foundation/system/diag": ["diag"], "foundation/system/platform": ["platform"],
    "foundation/system/sqlite": ["sqlite"], **{QIVEN3 + n: [n] for n in ("app", "checked", "entry", "location",
                                                                          "secret", "write")},
    QIVEN3 + "tests": ["driver", "child", "fuzzing"], "tools": ["tooling"]}


class Real(unittest.TestCase):
    """The checkout's own table, its exceptions and its rows in today's places pinned."""
    ctest_labels = ("fake",)

    def test_the_nine_exceptions(self):
        """base on platform, diag on log, structure on its seven: no tenth, none twice."""
        pairs = [(e["from"], to) for e in loop_modules.load()["exceptions"] for to in e["to"]]
        self.assertEqual(sorted(pairs), sorted([("base", "platform"), ("diag", "log")] + [
            ("structure", to) for to in ("store", "checked", "write", "location", "app", "driver", "child")]))

    def test_the_rows_in_todays_places(self):
        """Each row with a path outside its table file's directory, in that file."""
        table = loop_modules.load()
        found = {}
        for m in table["modules"]:
            where = table["homes"][m["name"]].rpartition("/")[0]
            if where and any(not p.startswith(where + "/") for p in m["paths"]):
                found.setdefault(where, []).append(m["name"])
        self.assertEqual(found, OUTSIDE)


if __name__ == "__main__":
    unittest.main()
