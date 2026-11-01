"""The build's globs and filters take their trees from tools/trees.json:
clang-tidy's header filters, the table's files, the Python files and
tests the listing finds, and the analysis's choice of a source by its path,
each read from foundation/devkit/build/trees.cmake as cmake -P gives it and
checked against the trees, and no build file naming a tree of its own."""

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
    args = [tool, f"-DROOT={root.as_posix()}", f"-DSHOW={show}"]
    if paths:
        args.append("-DPATHS=" + ";".join(paths))
    done = subprocess.run([*args, "-P", str(SCRIPT)], capture_output=True, text=True, timeout=120, check=True)
    return dict(line.split("=", 1) for line in done.stderr.splitlines() if "=" in line)


def analysed(path):
    """The rule, from the trees: under an analysed root, or under a layer's
    tree in no directory named tests."""
    if any(f"{path}/".startswith(f"{root}/") for root in TREES["analysed"]):
        return True
    return any(path.startswith(f"{layer}/") and "/tests/" not in f"/{path}" for layer in TREES["layers"])


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
        cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("${qiven3_filter_args} --quiet", cmake_text)

    def test_header_exclude(self):
        """A header the analysis does not read under a layer, a module's own
        tests/, is excluded by its directory; none excluded where none is."""
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "tools").mkdir()
            shutil.copy(ROOT / "tools" / "trees.json", root / "tools" / "trees.json")
            for header in ("foundation/core/m/include/qiven/m/m.hpp", "foundation/core/m/tests/t.hpp"):
                (root / header).parent.mkdir(parents=True)
                (root / header).write_text("", encoding="utf-8")
            got = shown("filters", root)
            self.assertTrue(got["exclude"].endswith("[/\\](foundation[/\\]core[/\\]m[/\\]tests)[/\\][^/\\]*$"),
                            got["exclude"])
            (root / "foundation/core/m/tests/t.hpp").unlink()
            self.assertEqual(shown("filters", root)["exclude"], "")

    def test_globs(self):
        """The table's files under each layer's tree, the Python files under
        the walked trees and the layers'; the build files name no tree."""
        got = shown("globs")
        root = ROOT.as_posix()
        self.assertEqual(got["table"].split(";"), [f"{root}/{layer}/module.json" for layer in TREES["layers"]])
        self.assertEqual(got["python"].split(";"),
                         [f"{root}/{tree}/*.py" for tree in (*TREES["walked"], *TREES["layers"])])
        tests = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("/foundation/module.json", tests)
        self.assertIn("qiven3_python_globs(", tests)
        self.assertIn("qiven3_table_globs(", tests)
        headers = (ROOT / "tests" / "support" / "headers" / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn('/src/*.hpp"', headers)
        self.assertIn("foreach(root IN LISTS qiven3_analysed)", headers)
        self.assertIn("foreach(layer IN LISTS qiven3_layers)", headers)

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
        self.assertEqual(got, {path: str(analysed(path)).upper() for path in paths})
        self.assertEqual([p for p in paths if analysed(p)],
                         ["src/memory/arena.cpp", "tests/support/log/log.cpp", "tests/child/main.cpp", "kits/kp/src/kp.cpp",
                          "foundation/devkit/test/runner/tests/runner_test.cpp",
                          "foundation/devkit/test/runner/src/runner.cpp"])
        cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn('qiven3_analysed("${rel}" read)', cmake_text)
        self.assertNotIn("qiven3_roots_regex", cmake_text)


if __name__ == "__main__":
    unittest.main()
