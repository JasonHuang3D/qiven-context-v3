"""The loop's affected tests, tools/loop_select.py, on recorded build graphs
in a scratch checkout: Ninja's log before and after a change, ctest's
listing, the Python tests' imports and declared reads. The plants: a
change in header selects header's and body's cases, whose binaries were
relinked, and not utc's, nor the Python test declaring utc's binary; a
planted Python test that reaches the build and declares nothing is
selected on any relink; a fixture's change, a vector's, a
CMakeLists.txt's, and a data file's that no narrower declaration than the
whole checkout names, each select every test; a vector's even where a
test declares tests/. Then the record through runs on fakes: a new tree
runs every test and records it; a run with nothing changed runs none; a
relink narrows the step by --tests-from-file and its line names the
selection; a failed step leaves its record, so that its change stays
selected; tier 3 narrows nothing."""

import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive.own_git import git, own_git
from tests.drive.test_loop_ci import load_loop
from tools import loop_git

LOOP = load_loop()
SELECT = LOOP.SELECT
TREE = "build/windows-msvc"
BINARIES = ("qiven3_unit_header", "qiven3_unit_body", "qiven3_unit_utc")
# The scratch checkout: the files a change plants, and the Python tests'
# modules, whose imports the selection reads by ast.
FILES = {
    ".gitignore": "build/\n",
    "CMakeLists.txt": "project(p)\n",
    "README.md": "readme\n",
    "src/entry/header.hpp": "// header\n",
    "src/entry/body.cpp": "// body\n",
    "src/utc/utc.cpp": "// utc\n",
    "tests/__init__.py": "",
    "tests/fixtures/entries.json": "[]\n",
    "tests/unit/log_plant.cpp": "// plant\n",
    "tests/drive/__init__.py": "",
    "tests/drive/binaries.py": "import os\n\nBIN = os.environ.get('QIVEN3_BIN')\n",
    "tests/drive/helper.py": "VALUE = 1\n",
    "tests/drive/test_utc.py": "from tests.drive import binaries\n",
    "tests/drive/test_reach.py": "from tests.drive import binaries\n",
    "tests/drive/test_plain.py": "from tests.drive.helper import VALUE\n",
    "tests/drive/test_plant.py": "import unittest\n",
}
# Each Python test's declared reads, as the build lists them.
READS = {
    "tests.drive.test_utc.Utc.test_zones": ("qiven3_unit_utc",),
    "tests.drive.test_plant.Field.test_plant": ("tests/unit/log_plant.cpp",),
}
PYTHON = ("tests.drive.test_utc.Utc.test_zones", "tests.drive.test_reach.Reach.test_runs",
          "tests.drive.test_plain.Plain.test_value", "tests.drive.test_plant.Field.test_plant")


def checkout(case):
    """A scratch checkout, its files in git's index, and its tree; the
    case's git and the selection's the fake (tests/drive/own_git.py): the
    property is the selection's, from the blob ids git gives."""
    top = Path(tempfile.mkdtemp(prefix="qiven3-loop-select-"))
    case.addCleanup(shutil.rmtree, top, ignore_errors=True)
    own_git(case, fake=True)
    for rel, text in FILES.items():
        (top / rel).parent.mkdir(parents=True, exist_ok=True)
        (top / rel).write_bytes(text.encode("utf-8"))
    git(top, "init", "-q")
    git(top, "add", "-A")
    built(top / TREE, {name: 100 for name in BINARIES})
    return top


def built(tree, stamps):
    """What a build leaves in the tree: Ninja's log, v7, one line an
    output, {binary: its mtime}; and the Python tests' declared reads."""
    (tree / "tests").mkdir(parents=True, exist_ok=True)
    (tree / SELECT["READS"]).write_text("".join(f"{t}\t{','.join(r)}\n" for t, r in READS.items()), encoding="utf-8")
    lines = ["# ninja log v7"] + [f"0\t9\t{stamp}\tbin/{name}.exe\t{stamp:x}" for name, stamp in stamps.items()]
    (Path(tree) / ".ninja_log").write_text("\n".join(lines) + "\n", encoding="utf-8")


def listing(tree):
    """ctest's listing: each binary's two cases behind the watcher, and the
    Python tests."""
    tests = []
    for name in BINARIES:
        short = name.removeprefix("qiven3_unit_")
        for case in ("one", "two"):
            binary = (Path(tree) / "bin" / f"{name}.exe").as_posix()
            tests.append((f"{short}.{case}", ["python", "-I", "watch.py", "--name", f"{short}.{case}", "--", binary,
                                              "--run", f"{short}.{case}"]))
    return tests + [(t, ["python", "-I", "-m", "unittest", "-k", "*" + t]) for t in PYTHON]


class Choose(unittest.TestCase):
    """choose() on a recorded graph: what each change selects."""
    ctest_labels = ("fake",)

    def setUp(self):
        self.root = checkout(self)
        self.tree = (self.root / TREE).resolve()
        self.outputs = {f"bin/{name}.exe": "1 1" for name in BINARIES}

    def choose(self, changed, relinked=()):
        relinked = {f"bin/{name}.exe" for name in relinked}
        return SELECT["choose"](listing(self.tree), changed, relinked, self.outputs, READS,
                                SELECT["Imports"](self.root), self.tree)

    def test_header(self):
        """The first plant: header's and body's cases, relinked; not utc's,
        nor the test declaring utc's binary; the undeclared test reaching
        the build, on any relink; not the one that reaches nothing."""
        names, why = self.choose(["src/entry/header.hpp"], ("qiven3_unit_header", "qiven3_unit_body"))
        self.assertEqual(sorted(names), ["body.one", "body.two", "header.one", "header.two",
                                         "tests.drive.test_reach.Reach.test_runs"])
        self.assertEqual(sorted(why), ["reaching the build", "relinked qiven3_unit_body",
                                       "relinked qiven3_unit_header"])

    def test_utc(self):
        """A relink of utc's binary selects utc's cases and the test that
        declares it, and the one reaching the build."""
        names, _ = self.choose(["src/utc/utc.cpp"], ("qiven3_unit_utc",))
        self.assertEqual(sorted(names), ["tests.drive.test_reach.Reach.test_runs",
                                         "tests.drive.test_utc.Utc.test_zones", "utc.one", "utc.two"])

    def test_every(self):
        """A fixture's change, a build file's and a tool's each run every
        test; so does a data file's that only the whole checkout names."""
        for path in ("tests/fixtures/entries.json", "CMakeLists.txt", "src/x.cmake", "tools/loop.py",
                     ".github/workflows/ci.yml", "README.md", "tests/fuzz/digest/regress/00"):
            with self.subTest(path=path):
                names, why = self.choose([path], ("qiven3_unit_header",))
                self.assertIsNone(names)
                self.assertIn(path, why)

    def test_vectors(self):
        """A vector's change runs every test as a fixture's does, by its
        directory, even where a test declares the whole of tests/: the
        binaries embed both at configure."""
        reads = dict(READS, **{"tests.drive.test_plain.Plain.test_value": ("tests/",)})
        for path in ("tests/fixtures/entries.json", "tests/vectors/ids.json"):
            with self.subTest(path=path):
                names, why = SELECT["choose"](listing(self.tree), [path], set(), self.outputs, reads,
                                              SELECT["Imports"](self.root), self.tree)
                self.assertIsNone(names)
                self.assertEqual(why, f"{path} changed")

    def test_imported_and_declared(self):
        """A module a test imports selects it; a declared file selects its
        test; nothing changed selects nothing."""
        names, why = self.choose(["tests/drive/helper.py"])
        self.assertEqual(names, ["tests.drive.test_plain.Plain.test_value"])
        self.assertEqual(list(why), ["importing tests/drive/helper.py"])
        names, why = self.choose(["tests/unit/log_plant.cpp"])
        self.assertEqual(names, ["tests.drive.test_plant.Field.test_plant"])
        names, why = self.choose(["tests/drive/binaries.py"])
        self.assertEqual(sorted(names), ["tests.drive.test_reach.Reach.test_runs",
                                         "tests.drive.test_utc.Utc.test_zones"])
        self.assertEqual(self.choose([]), ([], {}))

    def test_unknown_binary(self):
        """A case whose binary is no output of the build's log runs."""
        del self.outputs["bin/qiven3_unit_utc.exe"]
        names, why = self.choose([])
        self.assertEqual(names, ["utc.one", "utc.two"])
        self.assertEqual(list(why), ["a command of no output of the build"])


class Record(unittest.TestCase):
    """The record through tier 1's steps on fakes: the fake step narrowed
    as the change since it last passed in its tree says."""
    ctest_labels = ("fake",)

    def setUp(self):
        self.root = checkout(self)
        self.tree = self.root / TREE
        self.stamps = {name: 100 for name in BINARIES}
        self.failing = set()

    def run_tier(self, way="1"):
        """The way's windows-msvc unit on fakes; its fake step's argv, if it
        ran, and its line."""
        unit = LOOP.plan(way, "windows", ["windows-msvc"])[0]
        select = LOOP.SELECT["Selection"](self.root, "ctest", dict(os.environ), loop_git.which())
        select.listing = lambda words: listing(self.tree.resolve())
        unit.select = select
        presets = LOOP.binary_dirs()
        ran = {}

        def command(unit, step):
            argv = LOOP.local_argv(unit, step, presets)
            if step.name == "build":
                built(self.tree, self.stamps)
            if argv is None:
                return None
            ran[step.name] = argv
            return [sys.executable, "-I", "-c", f"raise SystemExit({int(step.name in self.failing)})"]

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            LOOP.drive(LOOP.Report(LOOP.LINES), [unit], self.root, self.root / "build" / "loop", command,
                       dict(os.environ))
        line = [x for x in out.getvalue().splitlines() if x.startswith("windows-msvc fake:")][0]
        return ran.get("fake"), line

    def relink(self, *names):
        for name in names:
            self.stamps[name] += 1

    def test_record(self):
        argv, line = self.run_tier()
        self.assertNotIn("--tests-from-file", argv)
        self.assertIn("every test, 10 of 10: its tree emptied, it held a build of no job of the loop", line)
        self.assertTrue((self.tree / SELECT["RECORD"]).is_file())
        argv, line = self.run_tier()
        self.assertIsNone(argv)
        self.assertIn("fake: not run, 0 of 10 tests: nothing it runs changed", line)
        (self.root / "src/entry/header.hpp").write_text("// header, changed\n", encoding="utf-8")
        self.relink("qiven3_unit_header", "qiven3_unit_body")
        argv, line = self.run_tier()
        chosen = Path(argv[argv.index("--tests-from-file") + 1])
        self.assertEqual(chosen.read_text(encoding="utf-8").split(),
                         ["header.one", "header.two", "body.one", "body.two",
                          "tests.drive.test_reach.Reach.test_runs"])
        self.assertIn("fake: pass in", line)
        self.assertIn("5 of 10 tests: 2 relinked qiven3_unit_header; 2 relinked qiven3_unit_body; "
                      "1 reaching the build", line)
        argv, line = self.run_tier()
        self.assertIsNone(argv)

    def test_failed_step_kept(self):
        """A failed step leaves its record: its change stays selected; once
        it passes, the record moves on."""
        self.run_tier()
        self.relink("qiven3_unit_utc")
        self.failing.add("fake")
        argv, line = self.run_tier()
        self.assertIn("fake: FAIL", line)
        self.assertIn("--tests-from-file", argv)
        self.failing.clear()
        argv, line = self.run_tier()
        self.assertIn("4 of 10 tests", line)
        self.assertIsNone(self.run_tier()[0])

    def test_emptied_tree(self):
        """Tier 3 empties the tree and narrows nothing; tier 1 after it,
        with nothing changed, runs nothing."""
        self.run_tier()
        self.relink("qiven3_unit_utc")
        argv, line = self.run_tier("3")
        self.assertNotIn("--tests-from-file", argv)
        self.assertRegex(line, r"^windows-msvc fake: pass in [0-9.]+ s(; git 0)?$")  # its git: where none counts it
        self.assertIsNone(self.run_tier()[0])
