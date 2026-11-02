"""The build's globs and filters take their trees from tools/trees.json:
clang-tidy's header filters, the table's files, the Python files and
tests the listing finds, the analysis's choice of a source by its path, and
a test source's clang-tidy configuration, each read from foundation/devkit/build/trees.cmake as cmake -P gives it and
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

from tests.structure import layers

ROOT = Path(__file__).resolve().parents[2]
TREES = json.loads((ROOT / "tools" / "trees.json").read_text(encoding="utf-8"))
SCRIPT = ROOT / "foundation" / "devkit" / "build" / "trees.cmake"
COMPOSE = ROOT / "foundation" / "devkit" / "build" / "tidy_config.cmake"
TEST_TIDY = "foundation/devkit/build/analysis-test.clang-tidy"
LISTING = runpy.run_path(str(ROOT / "foundation" / "devkit" / "test" / "runner" / "list_tests.py"))
LOOP = runpy.run_path(str(ROOT / "tools" / "loop_modules.py"))
HOMES = {home.rpartition("/")[0] for home in LOOP["tree"](ROOT)[0]["homes"].values()}


def cmake():
    """The cmake beside the ctest that runs the test, else the one on PATH."""
    ctest = os.environ.get("QIVEN3_CTEST")
    if ctest and Path(ctest).is_file():
        return str(Path(ctest).with_name("cmake" + (".exe" if sys.platform == "win32" else "")))
    return shutil.which("cmake")


def clang_tidy():
    """The clang-tidy on PATH, else LLVM's in Program Files; None where neither is."""
    llvm = Path(os.environ.get("ProgramFiles", ""), "LLVM", "bin")
    return shutil.which("clang-tidy") or shutil.which("clang-tidy", path=str(llvm))


def shown(show, root=ROOT, paths=(), defines=(), script=SCRIPT):
    """name=value lines trees.cmake (or script) prints for show, as a dict."""
    tool = cmake()
    if tool is None:
        raise unittest.SkipTest("no cmake")
    with tempfile.TemporaryDirectory() as scratch:
        listing = Path(scratch) / "paths.txt"
        listing.write_text("\n".join(paths) + "\n", encoding="utf-8")
        args = [tool, f"-DROOT={root.as_posix()}", f"-DSHOW={show}", f"-DPATHFILE={listing.as_posix()}",
                *(f"-D{define}" for define in defines)]
        done = subprocess.run([*args, "-P", str(script)], capture_output=True, text=True, timeout=120, check=True)
    return dict(line.split("=", 1) for line in done.stderr.splitlines() if "=" in line)


def compose(scratch, files, tidy, sources):
    """Each of sources' composed configuration as tidy_config.cmake gives it, a
    Path, in a checkout of files (path to text) under scratch."""
    root, build = Path(scratch, "code"), Path(scratch, "build")
    for path, text in files.items():
        (root / path).parent.mkdir(parents=True, exist_ok=True)
        (root / path).write_text(text, encoding="utf-8")
    paths = [(root / source).as_posix() for source in sources]
    got = shown("test-config", root=root, paths=paths, script=COMPOSE,
                defines=(f"BUILD={build.as_posix()}", f"TIDY={tidy}"))
    return [Path(got[path]) for path in paths]


def composed_options(config):
    """The CheckOptions block of a composed configuration, as a dict."""
    block = config.read_text(encoding="utf-8").split("\nCheckOptions:\n", 1)[1].split("\n")
    return dict(line.strip().split(": ", 1) for line in block[:next(
        i for i, line in enumerate(block) if not line.startswith("  "))])


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
        CMakeLists.txt above it names it, from that file's directory, alone
        or after a variable's directory, on whichever system, its comments
        and source properties (set_source_files_properties and
        set_property(SOURCE), in either case) stripped. One dropped from its
        list and left in place, named in a comment, a source property or
        under another directory, or under no CMakeLists.txt, leaves its
        header needing one; the refusal names the list read and the name
        looked for, so a list spelling it from another directory shows."""
        unit = "tests/support/headers/"
        path = "src/m/CMakeLists.txt"
        listed = "src/m/CMakeLists.txt names neither y.cpp nor ${...}/y.cpp outside its comments and source properties"
        for text in ("add_library(m)\n", "add_library(m) # y.cpp\n", "add_library(m other/y.cpp)\n", None,
                     'add_library(m)\nset_source_files_properties("${dir}/y.cpp" PROPERTIES COMPILE_OPTIONS -x)\n',
                     'add_library(m)\nSET_SOURCE_FILES_PROPERTIES("${dir}/y.cpp" PROPERTIES COMPILE_OPTIONS -x)\n',
                     'add_library(m)\nset_property(SOURCE "${dir}/y.cpp" PROPERTY COMPILE_OPTIONS -x)\n',
                     'add_library(m)\nSet_Property( SOURCE y.cpp PROPERTY COMPILE_OPTIONS -x)\n'):
            files, _ = scene()
            files[path] = text
            files = {name: body for name, body in files.items() if body is not None}
            status, _, errors = self.units(files)
            self.assertNotEqual(status, 0, text)
            why = listed if text else "no CMakeLists.txt is above it"
            self.assertIn(f"the header-only header src/m/y.hpp has no unit, {unit}m_y.cpp (its own source src/m/y.cpp "
                          f"is not listed: {why})", errors)
            files[f"{unit}m_y.cpp"] = '#include "m/y.hpp"\n'
            self.assertEqual(self.units(files)[:2], (0, sorted([*scene()[1], "m_y.cpp"])), text)
        files, _ = scene()
        files["kits/scene/CMakeLists.txt"] = 'set(dir src)\nadd_library(scene src/scene.cpp "${dir}/seen.cpp")\n'
        self.assertIn("(its own source kits/scene/src/seen.cpp is not listed: kits/scene/CMakeLists.txt names neither "
                      "src/seen.cpp nor ${...}/src/seen.cpp outside", self.units(files)[2])

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
        """CMake and Python read test code alike, and so the analysis's choice,
        CMake's qiven3_analysed and the structure checks' (B27's) reader,
        layers.Layers.analysed: every file of the tree, and files planted
        under a tests/ that is a module's, nested in one, or of no module's
        directory."""
        analysed = layers.of(ROOT).analysed
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

    def test_listing_reader(self):
        """The listing takes a Python test's package by the one reader: none
        where the reader says it is no test code, a module's src/tests/ among
        them; else the package below tests/, as the top-level tree holds it."""
        paths = {"kits/ai/src/tests/test_x.py": None, "foundation/core/memory/src/tests/test_x.py": None,
                 "tools/tests/test_x.py": "tests", "tests/test_x.py": None, "tests/drive/test_x.py": "drive",
                 "foundation/core/memory/tests/test_x.py": "tests",
                 "foundation/core/memory/tests/structure/test_x.py": "structure"}
        files = [p.relative_to(ROOT).as_posix() for tree in LOOP["TREES"] for p in (ROOT / tree).rglob("test_*.py")]
        got = {path: LISTING["package"](path, LOOP, HOMES) for path in [*files, *paths]}
        self.assertEqual({path: got[path] for path in paths}, paths)
        self.assertEqual([p for p, kind in got.items() if kind is None and LOOP["test_path"](p, HOMES)],
                         ["tests/test_x.py"])
        self.assertEqual([p for p, kind in got.items() if kind and not LOOP["test_path"](p, HOMES)], [])

    def test_test_config_out_of_tree(self):
        """A test source generated in a build tree outside the checkout,
        fixture_files.cpp, takes by the file its clang-tidy step names its
        directory's configuration in the checkout, the root's, under the test
        configuration: the root's checks and WarningsAsErrors, where the test
        configuration alone, searching up from the build tree, finds neither."""
        tidy = clang_tidy()
        if tidy is None:
            self.skipTest("no clang-tidy")

        def dumped(source, *config, flag="--list-checks"):
            """The checks clang-tidy enables for source, and its WarningsAsErrors line; or the line alone."""
            def run(option):
                return subprocess.run([tidy, *config, option, str(source), "--"], capture_output=True, text=True,
                                      timeout=120, check=True).stdout
            warnings = [line for line in run("--dump-config").splitlines() if line.startswith("WarningsAsErrors:")]
            return (sorted(run(flag).split()[2:]), warnings) if flag else warnings

        root = dumped(ROOT / "probe.cpp")
        self.assertIn("bugprone-use-after-move", root[0])
        self.assertNotIn(root[1], ([], ["WarningsAsErrors: ''"]))
        with tempfile.TemporaryDirectory() as build:
            self.assertFalse(Path(build).resolve().is_relative_to(ROOT))
            source = Path(build, "tests", "support", "fixtures", "fixture_files.cpp")
            source.parent.mkdir(parents=True)
            source.write_text("", encoding="utf-8")
            got = shown("test-config", paths=[source.as_posix()], defines=(f"BUILD={Path(build).as_posix()}",
                                                                           f"TIDY={tidy}"), script=COMPOSE)
            config = got[source.as_posix()]
            self.assertTrue(config.startswith(Path(build).as_posix() + "/tidy/config/"), config)
            self.assertEqual(dumped(source, f"--config-file={config}"), root)
            self.assertEqual(dumped(source, f"--config-file={ROOT / TEST_TIDY}", flag=None), ["WarningsAsErrors: ''"])

    def test_test_config_options(self):
        """The composed configuration keeps every CheckOptions entry of the
        configurations it reads, in the map form or the list, the later
        winning on a key, the analyzer's among them, which clang-tidy's dump
        drops: a scratch checkout's root and test configurations; and the
        analyzer reads an option kept, Pedantic's finding."""
        tidy = clang_tidy()
        if tidy is None:
            self.skipTest("no clang-tidy")
        pedantic, pointee = ("clang-analyzer-optin.cplusplus.UninitializedObject:Pedantic",
                             "clang-analyzer-core.CallAndMessage:ArgPointeeInitializedness")
        files = {".clang-tidy": ("Checks: '-*,bugprone-assert-side-effect,clang-analyzer-*'\nCheckOptions:\n"
                                 f"  # the root's\n  bugprone-assert-side-effect.CheckFunctionCalls: false\n"
                                 f"  {pointee}: true\n  {pedantic}: false\n"),
                 TEST_TIDY: (f"InheritParentConfig: true\nCheckOptions:\n  - key: {pedantic}\n    value: true\n"
                             "  - key: bugprone-assert-side-effect.CheckFunctionCalls\n    value: 'true'\n"),
                 "tests/probe.cpp": ("struct Pair {\n    int first;\n    int second;\n    Pair() {}\n};\n\n"
                                     "int main() {\n    Pair pair;\n    return sizeof(pair) > 0 ? 1 : 0;\n}\n")}
        with tempfile.TemporaryDirectory() as scratch:
            config = compose(scratch, files, tidy, ["tests/probe.cpp"])[0]
            source = Path(scratch, "code", "tests", "probe.cpp").as_posix()
            options = composed_options(config)
            self.assertEqual((options[pedantic], options[pointee]), ("true", "true"))
            self.assertEqual(options["bugprone-assert-side-effect.CheckFunctionCalls"], "'true'")
            self.assertNotIn(pedantic, config.with_name(config.name + ".dump").read_text(encoding="utf-8"))

            def found(name):
                return subprocess.run([tidy, f"--config-file={name}", source, "--"], capture_output=True,
                                      text=True, timeout=120, check=False).stdout
            self.assertIn("[clang-analyzer-optin.cplusplus.UninitializedObject", found(config))
            self.assertNotIn("UninitializedObject", found(config.with_name(config.name + ".dump")))

    def test_test_config_directories(self):
        """A source's composed configuration keeps its own directory's
        CheckOptions entries, over its parents' and under the test's, up the
        chain until a directory does not inherit; each directory its own file."""
        tidy = clang_tidy()
        if tidy is None:
            self.skipTest("no clang-tidy")
        checks = "Checks: '-*,bugprone-assert-side-effect'\n"
        files = {".clang-tidy": f"{checks}CheckOptions:\n  root.own: 1\n  every.level: root\n",
                 "tests/.clang-tidy": ("InheritParentConfig: true\nCheckOptions:\n  tests.own: 2\n"
                                       "  every.level: tests\n"),
                 "tests/deep/.clang-tidy": ("InheritParentConfig: True\nCheckOptions:\n  - key: deep.own\n"
                                            "    value: 3\n  - key: every.level\n    value: deep\n"),
                 "tests/apart/.clang-tidy": f"InheritParentConfig: false\n{checks}CheckOptions:\n  apart.own: 4\n",
                 TEST_TIDY: "InheritParentConfig: true\nCheckOptions:\n  test.own: 5\n"}
        sources = ["tests/one.cpp", "tests/deep/one.cpp", "tests/deep/two.cpp", "tests/apart/one.cpp"]
        with tempfile.TemporaryDirectory() as scratch:
            top, deep, deep_too, apart = compose(scratch, files, tidy, sources)
            kept = [{key: value for key, value in composed_options(config).items()
                     if not key.startswith("bugprone-")} for config in (top, deep, apart)]
        self.assertEqual(kept, [{"root.own": "1", "tests.own": "2", "every.level": "tests", "test.own": "5"},
                                {"root.own": "1", "tests.own": "2", "deep.own": "3", "every.level": "deep",
                                 "test.own": "5"},
                                {"apart.own": "4", "test.own": "5"}])
        self.assertEqual(deep, deep_too)
        self.assertEqual(len({top, deep, apart}), 3)

    def test_inherit_forms(self):
        """InheritParentConfig is read as clang-tidy reads it, each form it
        takes: the composition holds the parent's entries where clang-tidy's
        own dump shows the parent's checks. A file it cannot read, which a
        run skips with a line on stderr, stops the configure, naming the file."""
        tidy = clang_tidy()
        if tidy is None:
            self.skipTest("no clang-tidy")
        root = "Checks: '-*,bugprone-assert-side-effect,bugprone-dangling-handle'\nCheckOptions:\n  parent.own: 1\n"
        own = "Checks: 'bugprone-assert-side-effect'\nCheckOptions:\n  own.entry: 1\n"
        taken = {f"InheritParentConfig: {form}\n{own}": True
                 for form in ("true", "True", "TRUE", "yes", "Yes", "YES", "on", "On", "ON", "y", "Y", "'true'",
                              '"True"', "'yes'", "!!bool true", "!!str on", "&anchor True", "True # a comment")}
        taken.update({f"InheritParentConfig: {form}\n{own}": False
                      for form in ("false", "False", "FALSE", "no", "No", "NO", "off", "Off", "OFF", "n", "N",
                                   "'false'", '"No"', "!!bool false", "False # a comment")})
        taken.update({f"{form}{own}": True
                      for form in ("'InheritParentConfig': True\n", '"Inherit\\x50arentConfig": True\n',
                                   "InheritParentConfig : True\n", "InheritParentConfig:\n  True\n",
                                   "---\nInheritParentConfig: True\n")})
        taken["{InheritParentConfig: True, Checks: 'bugprone-assert-side-effect'}\n"] = True
        taken[own] = False
        files = {".clang-tidy": root, TEST_TIDY: "InheritParentConfig: true\n"}
        files.update({f"tests/f{i}/.clang-tidy": text for i, text in enumerate(taken)})
        with tempfile.TemporaryDirectory() as scratch:
            configs = compose(scratch, files, tidy, [f"tests/f{i}/x.cpp" for i in range(len(taken))])
            read = [("bugprone-dangling-handle" in config.with_name(config.name + ".dump").read_text(encoding="utf-8"),
                     "parent.own" in composed_options(config)) for config in configs]
        self.assertEqual(dict(zip(taken, read)), {text: (want, want) for text, want in taken.items()})
        for text in ("InheritParentConfig: tRuE\n", "InheritParentConfig: 1\n", "InheritParentConfig: ~\n",
                     "InheritParentConfig:true\n", "InheritParentConfig:\n", "inheritparentconfig: true\n"):
            with self.subTest(text), tempfile.TemporaryDirectory() as scratch:
                with self.assertRaises(subprocess.CalledProcessError) as stopped:
                    compose(scratch, {**files, "tests/.clang-tidy": text + own}, tidy, ["tests/x.cpp"])
                self.assertIn(f"{Path(scratch, 'code', 'tests').as_posix()}/.clang-tidy", stopped.exception.stderr)

    def test_steps_name_composed(self):
        """The analysis preset's build configured in a tree outside the
        checkout, its build.ninja read: each source the compile statements
        give a target of the configure's test-targets.txt has a clang-tidy
        step, and that step names the composed configuration the build
        derives for the source's own directory (tidy_config.cmake's own
        answer), one file for each directory and each directory one file;
        no step names the test configuration itself, and no other step a
        composed one. The list comes from the compile statements, not the steps."""
        tool, tidy = cmake(), clang_tidy()
        if tool is None or tidy is None or shutil.which("cl") is None:
            self.skipTest("no cmake, clang-tidy or cl: the analysis preset configures under vcvars")

        def key(path):
            return os.path.normcase(os.path.normpath(path.replace("$:", ":").replace("$ ", " ")))
        with tempfile.TemporaryDirectory() as build:
            self.assertFalse(Path(build).resolve().is_relative_to(ROOT))
            subprocess.run([tool, "-S", str(ROOT), "--preset", "analysis", "-B", build], capture_output=True,
                           text=True, timeout=900, check=True)
            targets = set(Path(build, "test-targets.txt").read_text(encoding="utf-8").split())
            ninja = Path(build, "build.ninja").read_text(encoding="utf-8")
            composed = key(Path(build, "tidy", "config").as_posix())
            compiled = re.findall(r"^build (?:\$ |\S)+: C(?:XX)?_COMPILER__(\S+)_(?:un)?scanned_\w+ ((?:\$ |\S)+)",
                                  ninja, re.M)
            listed = sorted({Path(source.replace("$:", ":").replace("$ ", " ")).as_posix()
                             for target, source in compiled if target in targets})
            tests = {key(source): key(config) for source, config in shown(
                "test-config", paths=listed, script=COMPOSE,
                defines=(f"BUILD={Path(build).as_posix()}", f"TIDY={tidy}")).items()}
            homes = {}
            for source, config in tests.items():
                base = key(build) if source.startswith(key(build) + os.sep) else key(str(ROOT))
                homes.setdefault(config, set()).add(os.path.relpath(os.path.dirname(source), base))
        steps = {}
        for line in ninja.splitlines():
            if "clang-tidy" in line and "--quiet" in line:
                named = re.findall(r'--config-file=([^"\s]+)', line)
                steps[key(re.search(r'--quiet\s+("[^"]+"|\S+)', line).group(1).strip('"'))] = named
        self.assertGreater(len(tests), 10)
        self.assertEqual(sorted(tests.keys() - steps.keys()), [])
        self.assertEqual({config: dirs for config, dirs in homes.items() if len(dirs) != 1}, {})
        self.assertEqual(sum(map(len, homes.values())), len(set().union(*homes.values())))
        self.assertGreater(len(homes), 1)
        self.assertEqual([config for config in homes if not config.startswith(composed + os.sep)], [])
        for source, named in sorted(steps.items()):
            self.assertEqual([name for name in named if name.endswith(Path(TEST_TIDY).name)], [], source)
            if source in tests:
                self.assertEqual([key(name) for name in named], [tests[source]], source)
            else:
                self.assertEqual([name for name in named if key(name).startswith(composed + os.sep)], [], source)


if __name__ == "__main__":
    unittest.main()
