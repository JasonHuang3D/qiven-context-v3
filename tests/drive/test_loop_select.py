"""The loop's affected tests, tools/loop_select.py, by the modules' table in
a scratch checkout, its tests listed by their modules' labels. What each
change selects: a module's file, or its own .cmake, that module and every
module depending on it, transitively; a new file its module and theirs; a
tools/ file the tooling alone; a change to a table file (the tree's
module.json files) the tooling, the module whose file it is and each
module whose row changed, with their dependents; a file of the no-test row
no module; any change, the tests that read every tracked file or every
test module, those alone of their module; a module's tests' dependencies read as its
library's; a file of the whole build, of the support or of no module every
test; on the checkout's table, a file of driver's that app's tests read
app, and a change under .github/ the tests that read it. Then the record through runs on fakes: a new tree runs every test
and records it; a run with nothing changed runs none; a change narrows the
step by --tests-from-file and its line names the modules and why; a
removed file selects its module; a failed step leaves its record, so that
its change stays selected; the modules' check failing fails the step,
which runs the check and no test, and its line names the error, by
Windows' command too, the check run by the interpreter; tier 3
narrows nothing."""

import ast
import contextlib
import fnmatch
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive.own_git import git, own_git
from tests.drive.test_loop_ci import load_loop
from tests.drive.test_loop_modules import built_file, from_tree, group_file, slot_file
from tools import loop_git

LOOP = load_loop()
SELECT = LOOP.SELECT
TREE = "build/windows-msvc"
TESTED = ("base", "utc", "entry", "write", "tooling")  # each module's two tests, ten in all
ROWS = [
    {"name": "build", "layer": "whole-build", "depends": [], "paths": ["CMakeLists.txt"]},
    {"name": "notes", "layer": "no-test", "depends": [], "paths": [".gitignore", "README.md"]},
    {"name": "support", "layer": "support", "depends": [], "paths": ["tests/__init__.py", "tests/support/"]},
    {"name": "base", "layer": "foundation", "depends": [], "paths": ["src/base/"]},
    {"name": "utc", "layer": "foundation", "depends": [], "paths": ["src/utc/"]},
    {"name": "entry", "layer": "product", "depends": [], "paths": ["src/entry/", "tests/unit/entry.cmake"]},
    {"name": "write", "layer": "product", "depends": ["entry"], "paths": ["src/write/"]},
    {"name": "tooling", "layer": "tooling", "depends": [], "paths": ["tools/"]}]
for _row in ROWS:  # entry's tests on base: the selection reads the union of the two
    _row["test_depends"] = ["base"] if _row["name"] == "entry" else []
# The table's files: a built file with two rows (utc's, base its first row),
# one with none and from, a slot, a group.
LAYOUT = {
    "module.json": built_file("repository", "repository", [ROWS[1], ROWS[0]]),
    "foundation/module.json": group_file("foundation", "foundation"),
    "foundation/core/utc/module.json": built_file("utc", "core", ROWS[3:5]),
    "foundation/core/time/module.json": built_file("time", "core", [], **{"from": ["utc"]}),
    "foundation/devkit/test/module.json": built_file("test", "devkit", ROWS[2:3]),
    "kits/ai/module.json": slot_file("ai", "kits"),
    "products/entry/module.json": built_file("entry", "products", ROWS[5:6]),
    "products/write/module.json": built_file("write", "products", ROWS[6:7]),
    "tools/module.json": built_file("tools", "tools", ROWS[7:]),
}
TABLE = from_tree(LAYOUT)[0]
FILES = {
    ".gitignore": "build/\n",
    "CMakeLists.txt": "project(p)\n",
    "README.md": "readme\n",
    "src/base/base.cpp": "// base\n",
    "src/utc/utc.cpp": "// utc\n",
    "src/entry/header.hpp": "// header\n",
    "src/write/write.cpp": "// write\n",
    "tests/__init__.py": "",
    "tests/support/main.cpp": "// main\n",
    "tests/unit/entry.cmake": "# entry\n",
    "tools/loop.py": "# loop\n",
    **{path: json.dumps(obj, indent=1) + "\n" for path, obj in LAYOUT.items()},
}


def reached(path, root, by_name):
    """The files the code of the Python file at path loads, starts or reads,
    docstrings no code: by an import, or by a string naming the file, whole
    from the root or by its name alone (a run_path's last part, a child's
    argv); by_name, {name: [paths]}."""
    nodes = list(ast.walk(ast.parse(path.read_text(encoding="utf-8"))))
    docs = {id(first.value) for n in nodes if isinstance(n, (ast.Module, ast.ClassDef, ast.FunctionDef))
            for first in n.body[:1] if isinstance(first, ast.Expr)}
    named = [name for n in nodes if isinstance(n, ast.Constant) and isinstance(n.value, str) and id(n) not in docs
             for name in re.findall(r"[\w./-]+\.py\b|trees\.json", n.value)]
    stems = [root / a.name.replace(".", "/") for n in nodes if isinstance(n, ast.Import) for a in n.names]
    for node in (n for n in nodes if isinstance(n, ast.ImportFrom)):
        package = root / node.module.replace(".", "/")
        stems += [package, *(package / a.name for a in node.names)]
    return ({file for stem in stems for file in (stem.with_suffix(".py"), stem / "__init__.py") if file.is_file()}
            | {file for name in named for file in by_name.get(Path(name).name, [])
               if (root / name).is_file() <= (file == root / name)})


def trees_readers(root):
    """{test module: its file} of each test module of the checkout at root
    that reads tools/trees.json: its own code naming the file, or reaching
    one that does (reached), through the tools', the structure tests' or any
    other Python file."""
    every = [p for tree in SELECT["MODULES"]["TREES"] for p in sorted((root / tree).rglob("*.py"))]
    by_name = {"trees.json": [root / "tools" / "trees.json"]}
    for path in every:
        by_name.setdefault(path.name, []).append(path)
    tests = [p for p in every if p.name.startswith("test_")]
    edges, waiting = {}, list(tests)
    while waiting:
        path = waiting.pop()
        if path.suffix == ".py" and path not in edges:
            edges[path] = reached(path, root, by_name)
            waiting += edges[path]
    readers = set(by_name["trees.json"])
    while new := {path for path, files in edges.items() if path not in readers and readers & files}:
        readers |= new
    return {rel.removesuffix(".py").replace("/", "."): rel
            for rel in (p.relative_to(root).as_posix() for p in tests if p in readers)}


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
    built(top / TREE)
    return top


def built(tree):
    """What a build leaves: its tree."""
    Path(tree).mkdir(parents=True, exist_ok=True)


def listing(tree=None):
    """ctest's listing: each module's two tests, labelled with its name."""
    return [(f"{name}.{case}", ["fake", name]) for name in TESTED for case in ("one", "two")]


def selection(root, errors=()):
    """The run's Selection on the scratch checkout: ctest's listing the
    fake's, the modules' check giving errors."""
    select = SELECT["Selection"](root, "ctest", dict(os.environ), loop_git.which())
    select.listing = lambda words: listing()
    select.check = lambda tree: (["the", "check"], list(errors))
    return select


def change(root, rel, text=None):
    """A file of the checkout changed, made, or removed (text None)."""
    path = Path(root) / rel
    if text is None:
        path.unlink()
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


class Choose(unittest.TestCase):
    """choose() and picked() on the table: what each change selects."""
    ctest_labels = ("fake",)

    def choose(self, changed, table=TABLE, rows=None):
        rows = {m["name"]: m for m in TABLE["modules"]} if rows is None else rows
        why, every = SELECT["choose"](table, rows, changed)
        if why is None:
            return None, every
        names = {m["name"] for m in table["modules"]}
        whole = {test: why.pop(test) for test in SELECT["WHOLE"] if test in why}  # any change's: apart
        self.assertEqual(whole, dict.fromkeys(SELECT["WHOLE"], "reading every tracked file") if changed else {})
        labelled = {test: why.pop(test) for test in SELECT["LABELLED"] if test in why}
        self.assertEqual(labelled, dict.fromkeys(SELECT["LABELLED"], "reading every test's labels") if changed else {})
        return SELECT["picked"](listing(), why, names)[0], why

    def test_whole(self):
        """Any change, a notes file's or a table file's among them, chooses
        the tests that read every tracked file, those alone of their module."""
        tests = [("tests.structure.test_content.Content.test_tree", ["fake", "structure"]),
                 ("tests.structure.test_content.Content.test_planted", ["fake", "structure"]),
                 ("tests.structure.test_boundaries.Tree.test_b1", ["fake", "structure"]), *listing()]
        names = {"structure", *(m["name"] for m in TABLE["modules"])}
        rows = {m["name"]: m for m in TABLE["modules"]}
        for changed, more in ((["README.md"], []), (["kits/ai/README.md"], []),
                              (["kits/ai/module.json"], ["tooling.one", "tooling.two"])):
            with self.subTest(changed=changed):
                chosen, reasons = SELECT["picked"](tests, SELECT["choose"](TABLE, rows, changed)[0], names)
                self.assertEqual(chosen, [tests[0][0], tests[2][0], *more])
                self.assertEqual(reasons[-1], "2 tests reading every tracked file")
        self.assertEqual(SELECT["picked"](tests, SELECT["choose"](TABLE, rows, [])[0], names)[0], [])

    def test_label_check_every_change(self):
        """A change outside the tooling, a notes file's among them, chooses
        the test that reads every test's labels, so that a class left
        without its label fails tier 1 wherever it is."""
        tests = [("tests.drive.test_tier_one.Reach.test_every_test_reached_or_kept_out", ["fake", "tooling"]),
                 *listing()]
        names = {"tooling", *(m["name"] for m in TABLE["modules"])}
        rows = {m["name"]: m for m in TABLE["modules"]}
        for changed in (["README.md"], ["src/utc/utc.cpp"]):
            with self.subTest(changed=changed):
                chosen, reasons = SELECT["picked"](tests, SELECT["choose"](TABLE, rows, changed)[0], names)
                self.assertEqual(chosen[0], tests[0][0])
                self.assertIn("1 tests reading every test's labels", reasons)

    def test_module_and_dependents(self):
        """A module's file, or its own .cmake: it and its dependents."""
        for path in ("src/entry/header.hpp", "tests/unit/entry.cmake"):
            with self.subTest(path=path):
                tests, why = self.choose([path])
                self.assertEqual(tests, ["entry.one", "entry.two", "write.one", "write.two"])
                self.assertEqual(why, {"entry": "changed", "write": "depending on entry"})

    def test_new_file(self):
        """A new file in a foundation module: its dependents, transitively."""
        tests, why = self.choose(["src/base/new.cpp"])
        self.assertEqual(tests, ["base.one", "base.two", "entry.one", "entry.two", "write.one", "write.two"])
        self.assertEqual(why, {"base": "changed", "entry": "depending on base", "write": "depending on base"})

    def test_tools_and_notes(self):
        """A tools/ file the tooling's tests alone; a notes file nothing."""
        self.assertEqual(self.choose(["tools/loop.py"]), (["tooling.one", "tooling.two"], {"tooling": "changed"}))
        self.assertEqual(self.choose(["README.md"]), ([], {}))
        self.assertEqual(self.choose([]), ([], {}))

    def test_every(self):
        """The whole build's file, the support's, a file in no module: every
        test, the reason naming the module or the file."""
        for path, said in (("CMakeLists.txt", "the whole build or the support: build changed (CMakeLists.txt)"),
                           ("tests/support/main.cpp", "support changed (tests/support/main.cpp)"),
                           ("odd/new.txt", "odd/new.txt in no module")):
            with self.subTest(path=path):
                tests, why = self.choose([path, "src/utc/utc.cpp"])
                self.assertIsNone(tests)
                self.assertIn(said, why)

    def test_table(self):
        """A table file changed: the tooling, the module whose file it is,
        and each module whose row changed, with their dependents; a row
        unchanged selects nothing. A slot's or a group's README.md, no table
        file, nothing."""
        self.assertEqual(self.choose(["kits/ai/module.json"])[1], {"tooling": "changed"})
        self.assertEqual(self.choose(["foundation/module.json"])[1], {"tooling": "changed"})
        self.assertEqual(self.choose(["kits/ai/README.md", "foundation/README.md"]), ([], {}))
        tests, why = self.choose(["products/entry/module.json"])
        self.assertEqual(why, {"entry": "changed", "write": "depending on entry", "tooling": "changed"})
        self.assertEqual(len(tests), 6)
        table = json.loads(json.dumps(TABLE))
        next(m for m in table["modules"] if m["name"] == "utc")["paths"].append("tests/unit/utc_test.cpp")
        tests, why = self.choose(["foundation/core/utc/module.json"], table)
        self.assertEqual(why, {"base": "changed", "utc": "changed in the table", "entry": "depending on base",
                               "write": "depending on base", "tooling": "changed"})
        self.assertEqual(len(tests), 10)
        self.assertIsNone(self.choose(["tools/module.json"], TABLE, {})[0])  # no rows recorded: the support's too

    def test_every_module_named(self):
        """More than three modules chosen for one reason: the line names each."""
        changed = ["src/base/a.cpp", "src/utc/utc.cpp", "src/entry/header.hpp", "src/write/w.cpp", "tools/loop.py"]
        why, _ = SELECT["choose"](TABLE, {m["name"]: m for m in TABLE["modules"]}, changed)
        reasons = SELECT["picked"](listing(), why, {m["name"] for m in TABLE["modules"]})[1]
        self.assertEqual(reasons, ["base, entry, tooling, utc, write changed"])
        self.assertEqual(SELECT["shown"](["utc", "entry", "base", "tooling"]), "base, entry, tooling, utc")
        self.assertEqual(SELECT["shown"](["d", "c", "b", "a"], SELECT["SHOWN"]), "a, b, c and 1 more")  # files

    def test_unlabelled(self):
        """A test with no module's label runs, the line counting it."""
        tests, reasons = SELECT["picked"]([("loose", ["fake"]), ("utc.one", ["utc"])], {}, {"utc"})
        self.assertEqual((tests, reasons), (["loose"], ["1 labelled with no module"]))

    def test_the_real_table(self):
        """The checkout's table: an edit of driver's files that app's tests
        read, or of driver's own, selects app; an edit of the helpers every
        module's tests read, the support's, selects every test."""
        table = SELECT["MODULES"]["load"]()
        rows = {m["name"]: m for m in table["modules"]}
        for path in ("tests/contract/platform_machine.py", "tests/contract/platform_location.py",
                     "tests/driver/common.cpp"):
            with self.subTest(path=path):
                why, every = SELECT["choose"](table, rows, [path])
                self.assertIsNone(every)
                self.assertEqual(why["app"], "depending on driver")
        for path in ("tests/drive/binaries.py", "tests/drive/session.py"):
            with self.subTest(path=path):
                self.assertIsNotNone(SELECT["choose"](table, rows, [path])[1])

    def test_workflows(self):
        """On the checkout's table, a change under .github/, the no-test
        row's: no module, the tests that read the workflows and those that
        read every tracked file; a notes file outside it, those alone."""
        table = SELECT["MODULES"]["load"]()
        rows = {m["name"]: m for m in table["modules"]}
        names = {m["name"] for m in table["modules"]}
        tests = [("tests.structure.test_triggers.Triggers.test_tree", ["fast", "structure"]),
                 ("tests.structure.test_triggers.Unpinned.test_planted_workflows", ["fast", "structure"]),
                 ("tests.drive.test_loop_ci.LoopAgainstCI.test_jobs", ["drive", "tooling"]),
                 ("tests.structure.test_content.Content.test_tree", ["fast", "structure"]),
                 ("tests.structure.test_content.Content.test_planted", ["fast", "structure"])]
        why, every = SELECT["choose"](table, rows, [".github/workflows/ci.yml"])
        self.assertIsNone(every)
        self.assertEqual(SELECT["picked"](tests, why, names),
                         ([t for t, _ in tests[:4]], ["3 tests reading .github/", "1 tests reading every tracked file"]))
        why, _ = SELECT["choose"](table, rows, ["README.md"])
        self.assertEqual(SELECT["picked"](tests, why, names)[0], [tests[3][0]])

    def test_trees_readers(self):
        """On the checkout's table, a change to tools/trees.json chooses every
        test module that reads it, by its own code or through what it imports
        or runs (trees_readers), by its name or its module's: a new reader
        cannot be left out, and this test runs on any change."""
        table = SELECT["MODULES"]["load"]()
        why, every = SELECT["choose"](table, {m["name"]: m for m in table["modules"]}, ["tools/trees.json"])
        self.assertIsNone(every)
        readers = trees_readers(Path(__file__).resolve().parents[2])
        self.assertLessEqual({"tests.drive.test_wiring", "tests.drive.test_differential"}, set(readers))
        owners = SELECT["MODULES"]["owners"](table, list(readers.values()))
        left = [name for name, rel in readers.items() if owners[rel][:1] not in ([m] for m in why)
                and not any(fnmatch.fnmatchcase(f"{name}.Case.test_case", test) for test in why)]
        self.assertEqual(left, [])

    def test_readers_closure(self):
        """The derivation on a scratch tree: a reader by its own string, by
        an import in a function through a run_path's last part, by a child's
        argv; none by a docstring, or in a cycle reaching no reader."""
        root = Path(tempfile.mkdtemp(prefix="qiven3-loop-readers-"))
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        for rel, text in {"tools/table.py": "TREES = ROOT / 'tools' / 'trees.json'\n",
                          "tests/structure/layers.py": "TABLE = ROOT / 'tools' / 'table.py'\n",
                          "tests/drive/test_own.py": "NAME = 'tools/trees.json'\n",
                          "tests/drive/test_deep.py": "def f():\n    from tests.structure import layers\n",
                          "tests/drive/test_child.py": "ARGV = ['-I', 'tools/table.py', '--help']\n",
                          "tests/drive/test_doc.py": "'''tools/trees.json, tools/table.py'''\n",
                          "tests/drive/test_cycle.py": "from tests.drive import test_ring\n",
                          "tests/drive/test_ring.py": "from tests.drive import test_cycle\n"}.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        self.assertEqual(sorted(trees_readers(root)), [f"tests.drive.test_{n}" for n in ("child", "deep", "own")])


class Record(unittest.TestCase):
    """The record through tier 1's steps on fakes: the fake step narrowed
    as the change since it last passed in its tree says."""
    ctest_labels = ("fake",)

    def setUp(self):
        self.root = checkout(self)
        self.tree = self.root / TREE
        self.failing, self.errors = set(), []

    def run_tier(self, way="1"):
        """The way's windows-msvc unit on fakes; its fake step's argv, if it
        ran, and its line."""
        unit = LOOP.plan(way, "windows", ["windows-msvc"])[0]
        unit.select = selection(self.root, self.errors)
        presets = LOOP.binary_dirs()
        ran = {}

        def command(unit, step):
            argv = LOOP.local_argv(unit, step, presets)
            if step.name == "build":
                built(self.tree)
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

    def chosen(self, argv):
        return Path(argv[argv.index("--tests-from-file") + 1]).read_text(encoding="utf-8").split()

    def test_record(self):
        argv, line = self.run_tier()
        self.assertNotIn("--tests-from-file", argv)
        self.assertIn("every test, 10 of 10: its tree emptied, it held a build of no job of the loop", line)
        self.assertIn('"table"', (self.tree / SELECT["RECORD"]).read_text(encoding="utf-8"))
        argv, line = self.run_tier()
        self.assertIsNone(argv)
        self.assertIn("fake: not run, 0 of 10 tests: nothing it runs changed", line)
        change(self.root, "src/entry/header.hpp", "// header, changed\n")
        argv, line = self.run_tier()
        self.assertEqual(self.chosen(argv), ["entry.one", "entry.two", "write.one", "write.two"])
        self.assertIn("fake: pass in", line)
        self.assertIn("4 of 10 tests: entry changed; write depending on entry", line)
        self.assertIsNone(self.run_tier()[0])

    def test_new_and_removed(self):
        """A new file selects its module and its dependents; a removed one
        its module."""
        self.run_tier()
        change(self.root, "src/base/new.cpp", "// new\n")
        argv, line = self.run_tier()
        self.assertIn("6 of 10 tests: base changed; entry, write depending on base", line)
        change(self.root, "src/utc/utc.cpp")
        argv, line = self.run_tier()
        self.assertEqual(self.chosen(argv), ["utc.one", "utc.two"])

    def test_failed_step_kept(self):
        """A failed step leaves its record: its change stays selected; once
        it passes, the record moves on."""
        self.run_tier()
        change(self.root, "src/utc/utc.cpp", "// utc, changed\n")
        self.failing.add("fake")
        argv, line = self.run_tier()
        self.assertIn("fake: FAIL", line)
        self.assertIn("--tests-from-file", argv)
        self.failing.clear()
        argv, line = self.run_tier()
        self.assertIn("2 of 10 tests: utc changed", line)
        self.assertIsNone(self.run_tier()[0])

    def test_check_fails(self):
        """The modules' check failing: the step runs the check, no test, its
        line naming the first error; the record stays."""
        self.run_tier()
        change(self.root, "src/utc/utc.cpp", "// utc, changed\n")
        self.errors += ["file odd.txt: in no module", "edge a -> b: foundation depending on product"]
        self.failing.add("fake")
        argv, line = self.run_tier()
        self.assertEqual(argv, ["the", "check"])
        self.assertIn("fake: FAIL", line)
        self.assertIn("the modules' check failed, no test run: file odd.txt: in no module, and 1 more", line)
        self.errors.clear()
        self.failing.clear()
        self.assertIn("2 of 10 tests: utc changed", self.run_tier()[1])

    def test_one_cause_one_fail(self):
        """The modules' check failing for the steps of one run: the first
        fails, the next with the same errors does not run and says why;
        other errors on a later step fail it."""
        select = SELECT["Selection"](self.root, "ctest", dict(os.environ), loop_git.which())
        unit = LOOP.plan("1", "windows", ["windows-msvc"])[0]
        step = {s.name: s for s in unit.steps}
        select.check = lambda tree: (["the", "check"], ["file odd.txt: in no module"])
        self.assertEqual(select.affected(unit, step["fake"], TREE, ["ctest"], True), ("the", "check"))
        self.assertIsNone(select.affected(unit, step["fast"], TREE, ["ctest"], True))
        self.assertEqual(unit.notes["fast"], "the modules' check failed before it, with the same errors")
        select.check = lambda tree: (["the", "check"], ["edge a -> b: foundation depending on product"])
        self.assertEqual(select.affected(unit, step["fast"], TREE, ["ctest"], True), ("the", "check"))

    def test_real_check(self):
        """The check itself, on a tree no configure made: it cannot run, and
        says so."""
        select = SELECT["Selection"](self.root, "ctest", dict(os.environ), loop_git.which())
        argv, errors = select.check(TREE)
        self.assertTrue(argv[2].endswith("loop_modules.py"), argv)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("no codemodel reply", errors[0])

    def test_check_fails_on_windows(self):
        """The check failing under Windows' command: the step runs the check
        by the interpreter, no tool of the pins, and fails, its line naming
        the check's error, its log the check's own lines."""
        line, log = self.check_on_windows()
        self.assertIn("fake: FAIL", line)
        self.assertIn("the modules' check failed, no test run: the check could not run: ", line)
        self.assertIn("no codemodel reply", line)
        self.assertIn("modules: the check could not run: ", log)

    def test_check_status_named(self):
        """The check run in the step's place, unable to run: its status, 2,
        named as the check's process's by its own words, not as ctest's."""
        line = self.check_on_windows()[0]
        self.assertRegex(line, r"^windows-msvc fake: FAIL, the modules' check's status: 2 \(error\): the check could "
                               r"not run, its error in its log, in [0-9.]+ s; the modules' check "
                               r"failed, no test run: the check could not run: ")
        self.assertNotIn("ctest's status", line)

    def check_on_windows(self):
        """The fake step under Windows' command, the check failing: its line
        and its log."""
        unit = LOOP.plan("1", "windows", ["windows-msvc"])[0]
        unit.select = SELECT["Selection"](self.root, "ctest", dict(os.environ), loop_git.which())
        logs = self.root / "build" / "loop"
        windows = LOOP.WINDOWS["windows_command"](self.root, logs, sys.executable, {}, (), LOOP.binary_dirs())

        def command(unit, step):
            if step.name == "fake":
                return windows(unit, step)
            argv = LOOP.local_argv(unit, step, LOOP.binary_dirs())
            return None if argv is None else [sys.executable, "-I", "-c", ""]

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            LOOP.drive(LOOP.Report(LOOP.LINES), [unit], self.root, logs, command, dict(os.environ))
        line = [x for x in out.getvalue().splitlines() if x.startswith("windows-msvc fake:")][0]
        return line, next(logs.rglob("*-fake.log")).read_text(encoding="utf-8", errors="replace")

    def test_emptied_tree(self):
        """Tier 3 empties the tree and narrows nothing; tier 1 after it,
        with nothing changed, runs nothing."""
        self.run_tier()
        change(self.root, "src/utc/utc.cpp", "// utc, changed\n")
        argv, line = self.run_tier("3")
        self.assertNotIn("--tests-from-file", argv)
        self.assertRegex(line, r"^windows-msvc fake: pass in [0-9.]+ s(; git 0)?$")  # its git: where none counts it
        self.assertIsNone(self.run_tier()[0])
