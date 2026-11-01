"""The build's globs and filters take their trees from tools/trees.json:
clang-tidy's header filters, the table's files, the Python files and
tests the listing finds, and the analysis's choice of a source by its path,
each read from foundation/devkit/build/trees.cmake as cmake -P gives it and
checked against the trees and, for test code, Python's reader."""

import json
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TREES = json.loads((ROOT / "tools" / "trees.json").read_text(encoding="utf-8"))
SCRIPT = ROOT / "foundation" / "devkit" / "build" / "trees.cmake"
LISTING = runpy.run_path(str(ROOT / "foundation" / "devkit" / "test" / "runner" / "list_tests.py"))
LOOP = runpy.run_path(str(ROOT / "tools" / "loop_modules.py"))
HOMES = {home.rpartition("/")[0] for home in LOOP["tree"](ROOT)[0]["homes"].values()}


def cmake():
    """The cmake beside the ctest that runs the test, else the one on PATH."""
    ctest = os.environ.get("QIVEN3_CTEST")
    if ctest and Path(ctest).is_file():
        return str(Path(ctest).with_name("cmake" + (".exe" if sys.platform == "win32" else "")))
    return shutil.which("cmake")


def shown(show, root=ROOT, paths=()):
    """name=value lines trees.cmake prints for show, as a dict."""
    tool = cmake()
    if tool is None:
        raise unittest.SkipTest("no cmake")
    with tempfile.TemporaryDirectory() as scratch:
        listing = Path(scratch) / "paths.txt"
        listing.write_text("\n".join(paths) + "\n", encoding="utf-8")
        args = [tool, f"-DROOT={root.as_posix()}", f"-DSHOW={show}", f"-DPATHFILE={listing.as_posix()}"]
        done = subprocess.run([*args, "-P", str(SCRIPT)], capture_output=True, text=True, timeout=120, check=True)
    return dict(line.split("=", 1) for line in done.stderr.splitlines() if "=" in line)


def analysed(path):
    """The rule, by Python's reader of test code: under an analysed root, as
    it stands or as the top-level tests/ would hold it, or under a layer's
    tree and no test code."""
    test = LOOP["test_path"](path, HOMES)
    if any(f"{where}/".startswith(f"{root}/") for where in (path, test or "") for root in TREES["analysed"]):
        return True
    return test is None and any(path.startswith(f"{layer}/") for layer in TREES["layers"])


def scene():
    """A scratch checkout's files (path to text), each unit and own source
    including its header, and the units the rule wants of it."""
    unit = "tests/support/headers/"
    files = {"kits/module.json": '{"name": "kits"}', "kits/business/module.json": '{"name": "business"}',
             "kits/scene/module.json": '{"name": "scene"}', "kits/business/src/detail.hpp": "",
             "kits/scene/src/detail.hpp": "", "kits/scene/src/scene.cpp": "",
             "kits/scene/include/qiven/scene/seen.hpp": "",
             "kits/scene/src/seen.cpp": '#include "qiven/scene/seen.hpp"\n', "kits/scene/include/qiven/scene/only.hpp": "", "kits/scene/tests/case.hpp": "",
             "kits/scene/CMakeLists.txt": "add_library(scene src/scene.cpp src/seen.cpp)\n",
             "src/m/CMakeLists.txt": 'if(WIN32)\n  add_library(m "${dir}/y.cpp")\nendif()\n',
             "src/m/x.hpp": "", "src/m/y.hpp": "", "src/m/y.cpp": '#include "y.hpp"\n', "tests/support/m/x.hpp": "",
             "tests/child/c.hpp": "", "tests/driver/d.hpp": ""}
    includes = {"business_src_detail.cpp": "../../../kits/business/src/detail.hpp", "child_c.cpp": "child/c.hpp",
                "m_x.cpp": "m/x.hpp", "scene_only.cpp": "qiven/scene/only.hpp",
                "scene_src_detail.cpp": "../../../kits/scene/src/detail.hpp", "support_m_x.cpp": "support/m/x.hpp"}
    files.update({unit + name: f'#include "{header}"\n' for name, header in includes.items()})
    return files, sorted(includes)


class TreesBuild(unittest.TestCase):
    ctest_labels = ("fake",)  # tier 1's fake step runs it
    def test_header_filter(self):
        """The header filter is the analysed roots' and the layers' trees
        under this checkout alone, either separator; no .clang-tidy names one;
        the tidy steps take it."""
        got = shown("filters")
        trees = "|".join(t.replace("/", "[/\\]") for t in (*TREES["analysed"], *TREES["layers"]))
        filter_ = got["filter"]
        self.assertTrue(filter_.endswith(f"[/\\]({trees})[/\\]"), filter_)
        regex = re.compile(filter_.replace("[/\\]", "[/\\\\]"), re.IGNORECASE)
        self.assertTrue(regex.search(f"{ROOT.as_posix()}/foundation/devkit/test/runner/include/qiven/runner/core.hpp"))
        self.assertTrue(regex.search(str(ROOT / "src" / "memory" / "arena.hpp")))
        self.assertFalse(regex.search(f"{ROOT.as_posix()}/tests/driver/drive.hpp"))
        self.assertFalse(regex.search(f"{ROOT.parent.as_posix()}/src/any/x.hpp"), "a src/ beside the checkout")
        for config in ROOT.rglob(".clang-tidy"):
            if "build" in config.relative_to(ROOT).parts[:1]:
                continue
            self.assertNotRegex(config.read_text(encoding="utf-8"), r"(?m)^\s*(Exclude)?HeaderFilterRegex", config)

    def test_header_exclude(self):
        """A header the analysis does not read under a layer, a module's own
        tests/, is excluded by its directory; none excluded where none is."""
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "tools").mkdir()
            shutil.copy(ROOT / "tools" / "trees.json", root / "tools" / "trees.json")
            for header in ("foundation/core/m/include/qiven/m/m.hpp", "foundation/core/m/tests/t.hpp",
                           "foundation/core/m/module.json", "foundation/core/m/src/tests/read.hpp",
                           "foundation/core/m/tests/support/read.hpp"):
                (root / header).parent.mkdir(parents=True, exist_ok=True)
                (root / header).write_text("", encoding="utf-8")
            got = shown("filters", root)
            self.assertTrue(got["exclude"].endswith("[/\\](foundation[/\\]core[/\\]m[/\\]tests)[/\\][^/\\]*$"),
                            got["exclude"])
            (root / "foundation/core/m/tests/t.hpp").unlink()
            self.assertEqual(shown("filters", root)["exclude"], "")
            for suffix in TREES["header_suffixes"]:
                (root / f"foundation/core/m/tests/t{suffix}").write_text("", encoding="utf-8")
                self.assertNotEqual(shown("filters", root)["exclude"], "", suffix)
                (root / f"foundation/core/m/tests/t{suffix}").unlink()

    def test_globs(self):
        """The table's files under each layer's tree, the Python files under
        the walked trees and the layers'."""
        got = shown("globs")
        root = ROOT.as_posix()
        self.assertEqual(got["table"].split(";"), [f"{root}/{layer}/module.json" for layer in TREES["layers"]])
        self.assertEqual(got["python"].split(";"),
                         [f"{root}/{tree}/*.py" for tree in (*TREES["walked"], *TREES["layers"])])

    def units(self, files):
        """trees.cmake's header units for a scratch checkout of files (path to
        text): the exit status, the units and the errors' words."""
        tool = cmake()
        if tool is None:
            raise unittest.SkipTest("no cmake")
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "tools").mkdir()
            shutil.copy(ROOT / "tools" / "trees.json", root / "tools" / "trees.json")
            for name, text in files.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text, encoding="utf-8")
            done = subprocess.run([tool, f"-DROOT={root.as_posix()}", "-DSHOW=units",
                                   f"-DPython3_EXECUTABLE={sys.executable}", "-P", str(SCRIPT)],
                                  capture_output=True, text=True, timeout=120)
        shown_units = [line.split("=", 1)[1] for line in done.stderr.splitlines() if line.startswith("units=")]
        return done.returncode, sorted(filter(None, "".join(shown_units).split(";"))), " ".join(done.stderr.split())

    def test_header_units(self):
        """Each header the analysis reads with no source of its own has a unit
        whose name carries its module, whether or not its module has sources;
        configure refuses such a header with no unit, two headers of one unit
        and a unit no such header wants."""
        unit = "tests/support/headers/"
        files, wanted = scene()
        status, got, _ = self.units(files)
        self.assertEqual((status, got), (0, wanted))
        del files[unit + "scene_src_detail.cpp"]
        status, _, errors = self.units(files)
        self.assertNotEqual(status, 0)
        self.assertIn(f"the header-only header kits/scene/src/detail.hpp has no unit, {unit}scene_src_detail.cpp",
                      errors)
        self.assertNotIn("kits/business/src/detail.hpp", errors)
        files[unit + "scene_src_detail.cpp"] = '#include "../../../kits/scene/src/detail.hpp"\n'
        files[unit + "scene_seen.cpp"] = ""
        status, _, errors = self.units(files)
        self.assertNotEqual(status, 0)
        self.assertIn(f"{unit}scene_seen.cpp is no header-only header's unit", errors)
        del files[unit + "scene_seen.cpp"]
        files["kits/scene/src/detail/x.hpp"] = ""
        files["kits/scene/src/detail_x.hpp"] = ""
        files[unit + "scene_src_detail_x.cpp"] = ""
        status, _, errors = self.units(files)
        self.assertNotEqual(status, 0)
        self.assertIn(f"the headers kits/scene/src/detail/x.hpp and kits/scene/src/detail_x.hpp share the unit "
                      f"{unit}scene_src_detail_x.cpp", errors)
        status, got, errors = self.units({})
        self.assertEqual((status, got), (0, []), errors)

    def test_support_moved(self):
        """A shared test header moved into a module's own tests/ stays read:
        it wants a unit as it did, the unit named from its new place."""
        files, wanted = scene()
        del files["tests/support/m/x.hpp"]
        files["kits/scene/tests/support/m/x.hpp"] = ""
        del files["tests/support/headers/support_m_x.cpp"]
        _, _, errors = self.units(files)
        self.assertIn("the header-only header kits/scene/tests/support/m/x.hpp has no unit, "
                      "tests/support/headers/scene_tests_support_m_x.cpp", errors)

    def test_header_includes(self):
        """A unit, or an own source standing in for one, includes its header,
        as the lexer reads it and the include roots resolve it; a header of
        each suffix of the rule wants a unit."""
        unit = "tests/support/headers/"
        for text in ("// nothing\n", '#include "m/y.hpp"\n', '// #include "m/x.hpp"\n'):
            files, _ = scene()
            files[unit + "m_x.cpp"] = text
            status, _, errors = self.units(files)
            self.assertNotEqual(status, 0, text)
            self.assertIn(f"the unit {unit}m_x.cpp does not include its header src/m/x.hpp", errors)
        for text in ("#include <m/x.hpp>\n", '#include "../../../src/m/x.hpp"\n'):
            files, wanted = scene()
            files[unit + "m_x.cpp"] = text
            self.assertEqual(self.units(files)[:2], (0, wanted), text)
        for own, header, name in (("kits/scene/src/seen.cpp", "kits/scene/include/qiven/scene/seen.hpp", "scene_seen"),
                                  ("src/m/y.cpp", "src/m/y.hpp", "m_y")):
            files, wanted = scene()
            files[own] = "// nothing\n"
            status, _, errors = self.units(files)
            self.assertNotEqual(status, 0)
            self.assertIn(f"the header-only header {header} has no unit, {unit}{name}.cpp (its own source {own} "
                          "does not include it)", errors)
            files[f"{unit}{name}.cpp"] = f'#include "../../../{header}"\n'
            self.assertEqual(self.units(files)[:2], (0, sorted([*wanted, f"{name}.cpp"])))
        for header, name in (("src/m/t.h", "m_t"), ("kits/scene/src/thing.hxx", "scene_src_thing"),
                             ("tests/support/m/u.hh", "support_m_u")):
            files, wanted = scene()
            files[header] = "template <typename T> T same(T t) { return t; }\n"
            status, _, errors = self.units(files)
            self.assertNotEqual(status, 0, header)
            self.assertIn(f"the header-only header {header} has no unit, {unit}{name}.cpp", errors)
            files[f"{unit}{name}.cpp"] = f'#include "../../../{header}"\n'
            self.assertEqual(self.units(files)[:2], (0, sorted([*wanted, f"{name}.cpp"])))

    def test_every_suffix(self):
        """A header of every suffix wants a unit; a source of every suffix, beside it or in src/, is its own."""
        for suffix in TREES["header_suffixes"]:
            files, _ = scene()
            files[f"src/m/t{suffix}"] = ""
            self.assertIn(f"the header-only header src/m/t{suffix} has no unit, tests/support/headers/m_t.cpp",
                          self.units(files)[2])
        for suffix in TREES["source_suffixes"]:
            files, wanted = scene()
            files |= {"src/m/t.hpp": "", f"src/m/t{suffix}": '#include "t.hpp"\n',
                      "kits/scene/include/qiven/scene/t.hpp": "", f"kits/scene/src/t{suffix}": '#include "qiven/scene/t.hpp"\n'}
            files["src/m/CMakeLists.txt"] += f"add_library(t t{suffix})\n"
            files["kits/scene/CMakeLists.txt"] += f"add_library(t src/t{suffix})\n"
            self.assertEqual(self.units(files)[:2], (0, wanted), suffix)

    def test_own_source_compiled(self):
        """An own source stands in for its header's unit only where the
        CMakeLists.txt above it lists it, on whichever system: one dropped
        from its list and left in place, named in a comment, a source
        property or under another directory, or under no CMakeLists.txt,
        leaves its header needing one."""
        unit = "tests/support/headers/"
        path = "src/m/CMakeLists.txt"
        for text in ("add_library(m)\n", "add_library(m) # y.cpp\n", "add_library(m other/y.cpp)\n", None,
                     'add_library(m)\nset_source_files_properties("${dir}/y.cpp" PROPERTIES COMPILE_OPTIONS -x)\n'):
            files, _ = scene()
            files[path] = text
            files = {name: body for name, body in files.items() if body is not None}
            status, _, errors = self.units(files)
            self.assertNotEqual(status, 0, text)
            self.assertIn(f"the header-only header src/m/y.hpp has no unit, {unit}m_y.cpp (its own source src/m/y.cpp "
                          "is in no target's sources)", errors)
            files[f"{unit}m_y.cpp"] = '#include "m/y.hpp"\n'
            self.assertEqual(self.units(files)[:2], (0, sorted([*scene()[1], "m_y.cpp"])), text)

    def test_discovery(self):
        """The listing discovers in every top package of the walked trees and
        the layers': a module's own tests/ package under a layer is found."""
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for init in ("tests/__init__.py", "tests/drive/__init__.py", "foundation/core/m/tests/__init__.py",
                         "foundation/core/m/tests/__pycache__/__init__.py", "kits/k/x/__init__.py"):
                (root / init).parent.mkdir(parents=True, exist_ok=True)
                (root / init).write_text("", encoding="utf-8")
            found = LISTING["packages"](root, ("tests", "tools", "foundation", "kits"))
            self.assertEqual([p.relative_to(root).as_posix() for p in found],
                             ["foundation/core/m/tests", "kits/k/x", "tests"])
            self.assertEqual(LISTING["unpackaged"](root, ("tests", "foundation", "kits")), [])
            for test in ("kits/k/tests/test_p.py", "tests/drive/test_q.py", "tests/plain/test_r.py"):
                (root / test).parent.mkdir(parents=True, exist_ok=True)
                (root / test).write_text("", encoding="utf-8")
            self.assertEqual([p.relative_to(root).as_posix() for p in LISTING["unpackaged"](root, ("tests", "kits"))],
                             ["kits/k/tests", "tests/plain"])
        rows, errors = LISTING["listed"](False, ROOT)
        self.assertEqual(errors, [])
        self.assertTrue(all(row[0].split(".", 1)[0] in (*TREES["walked"], *TREES["layers"]) for row in rows))

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
            lines = "".join(f"{test_id}\tdrive\t0\t\n" for test_id in starts)
            files = {"tests/__init__.py": "", "tests/drive/__init__.py": "", "foundation/core/m/tests/__init__.py": "",
                     "foundation/devkit/test/runner/list_tests.py": f"print({lines!r}, end='')\n", "inputs.txt": ""}
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
                    "-P", str(ROOT / "foundation" / "devkit" / "test" / "runner" / "ctest_tests.cmake")]
            subprocess.run(args, capture_output=True, text=True, timeout=120, check=True)
            added = re.findall(r"(?m)^add_test\((.*)\)$", out.read_text(encoding="utf-8"))
            self.assertEqual(len(added), len(starts))
            for line in added:
                name, *words = re.findall(r"\[==\[(.*?)\]==\]", line)
                command = words[words.index("--") + 1:]
                self.assertEqual(command[1:], ["-I", "-m", "unittest", "discover", "-s", starts[name], "-t", ".",
                                               "-k", f"*{name}"])
                done = subprocess.run([sys.executable, *command[1:]], cwd=root, capture_output=True, text=True,
                                      timeout=120)
                self.assertEqual(done.returncode, 0, done.stderr)
                self.assertIn("Ran 1 test", done.stderr)

    def test_analysis_choice(self):
        """The analysis chooses a source by its path, by the trees' rule, not
        by the directory that defines its target."""
        paths = ("src/memory/arena.cpp", "tests/support/log/log.cpp", "tests/child/main.cpp", "tests/unit/arena_test.cpp",
                 "tests/driver/driver.cpp", "foundation/core/memory/tests/arena_test.cpp", "kits/kp/src/kp.cpp",
                 "foundation/devkit/test/runner/tests/runner_test.cpp", "foundation/devkit/test/runner/src/runner.cpp",
                 "products/qiven3/tests/x/drive.cpp", "third_party/sqlite/vendored.c")
        got = shown("analysed", paths=paths)
        self.assertEqual([p for p in paths if got[p].startswith("TRUE")],
                         ["src/memory/arena.cpp", "tests/support/log/log.cpp", "tests/child/main.cpp", "kits/kp/src/kp.cpp",
                          "foundation/devkit/test/runner/tests/runner_test.cpp",
                          "foundation/devkit/test/runner/src/runner.cpp"])

    def test_one_reader(self):
        """CMake and Python read test code alike, and so the analysis's choice:
        every file of the tree, and files planted under a tests/ that is a
        module's, nested in one, or of no module's directory."""
        planted = ("src/tests/x.cpp", "kits/ai/src/tests/x.cpp", "kits/ai/include/qiven/ai/tests/h.hpp",
                   "foundation/core/memory/src/tests/x.cpp", "foundation/core/memory/tests/x.cpp",
                   "foundation/core/memory/tests/support/x.hpp", "foundation/core/memory/tests/child/x.cpp",
                   "foundation/core/memory/tests/a/tests/b.cpp", "tools/tests/x.py", "tests/a/tests/b.cpp")
        files = [p.relative_to(ROOT).as_posix() for tree in LOOP["TREES"] for p in (ROOT / tree).rglob("*")
                 if p.is_file() and "__pycache__" not in p.parts]
        got = shown("analysed", paths=[*files, *planted])
        self.assertEqual(got, {path: f"{str(analysed(path)).upper()} {LOOP['test_path'](path, HOMES) or ''}"
                               for path in [*files, *planted]})
        self.assertEqual(got["foundation/core/memory/src/tests/x.cpp"], "TRUE ")
        self.assertEqual(got["foundation/core/memory/tests/support/x.hpp"], "TRUE tests/support/x.hpp")
        self.assertEqual(got["foundation/core/memory/tests/a/tests/b.cpp"], "FALSE tests/a/tests/b.cpp")


if __name__ == "__main__":
    unittest.main()
