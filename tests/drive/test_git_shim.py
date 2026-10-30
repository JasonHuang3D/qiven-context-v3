"""The tests' git by the path: on the build QIVEN3_LOOP_TESTS names, one a
system, every Python test runs behind the counting shim, which stands
first on PATH under git's program name (tools/loop_shim.py), so that a
start of git by any name PATH resolves is counted, the door's or not; the
loop's own tests, of a class whose ctest_git is true, are listed there
alone (tests/runner/list_tests.py), and the shim fails a test of no such
class that starts git, so that no test that starts git runs in a second
step of a run. A start by an absolute path passes the shim: the guard's,
by the source (tests/drive/test_git_count.py), the stated limit.

Shim plants, in a scratch tree, a module starting git by a name built at
run time and by a shell's line, a module named test*.py but not test_*.py
starting it through the door, and one reaching the door by runpy: each
start is counted in the enclosing count, the first fails as past the door,
and each fails unless it is the loop's own.

Plan reads each tier's plan on each system as CMake and CTest do: a tree
registers the loop's own tests when the configure that wrote it, in the
same plan, sets QIVEN3_LOOP_TESTS true and QIVEN3_STRUCTURE not
(tests/CMakeLists.txt), by its preset's cacheVariables or its own -D in
any form, the last one standing; a step runs one when its test preset's
filter and its own -L, -LE, -R and -E select it, each -L and -LE of them
all, the last -R and -E. One step a run so selects them, so that the
steps' bounds fit the run's. CMake's reading of true and CTest's of the
filters are as a scratch project measured them (CMake and CTest 4.4.3).

Exempt checks that each file the guard exempts but the door starts no
process but those it names."""

import ast
import functools
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

import tests.drive.test_git_count
from tests.drive.test_loop_ci import ROOT, load_loop
from tools import loop_git

SHIM = ROOT / "tools" / "loop_shim.py"
# A call that starts a process, by its dotted name; and the files the guard
# exempts but the door, with the programs their calls may start.
STARTS = re.compile(r"(?:subprocess|multiprocessing|pty)\.\w+|os\.(?:system|popen|exec\w*|spawn\w*|posix_spawn\w*|"
                    r"startfile|fork\w*)")
STARTING_NO_GIT = {"tests/drive/test_git_count.py": set(), "tests/structure/rules.py": set(),
                   "tools/loop_shim.py": {"cl", "command"}}
# CMake's false constants, in any case, but NOTFOUND's suffix, in its case.
FALSE = {"", "0", "N", "NO", "OFF", "FALSE", "IGNORE"}
# The options a plan's cmake and ctest lines may give, with the words each
# takes; any other fails the reading.
CMAKE_WORDS = {"--preset": 1, "-B": 1}
CTEST_WORDS = {"--preset": 1, "--test-dir": 1, "--output-on-failure": 0, "-L": 1, "--label-regex": 1, "-LE": 1,
               "--label-exclude": 1, "-R": 1, "--tests-regex": 1, "-E": 1, "--exclude-regex": 1, "--no-tests=": 0}
FILTERS = {"-L": "label_in", "--label-regex": "label_in", "-LE": "label_out", "--label-exclude": "label_out",
           "-R": "name_in", "--tests-regex": "name_in", "-E": "name_out", "--exclude-regex": "name_out"}
OUTSIDE = (b"import subprocess\nimport unittest\n\n\nclass PlantedOutside(unittest.TestCase):\n"
           b"    def test_outside(self):\n        name = ''.join(('g', 'i', 't'))\n"
           b"        subprocess.run([name, '--version'], check=True, capture_output=True)\n"
           b"        subprocess.run(name + ' --version', shell=True, check=True, capture_output=True)\n")
NAMED = (b"import unittest\n\nfrom tools import loop_git\n\n\nclass PlantedName(unittest.TestCase):\n"
         b"    def test_door(self):\n        loop_git.run(loop_git.which(), ['--version'], check=True)\n")
BY_RUNPY = (b"import runpy\nimport unittest\n\n\nclass PlantedRunpy(unittest.TestCase):\n"
            b"    def test_door(self):\n        door = runpy.run_path('tools/loop_git.py')\n"
            b"        door['run'](door['which'](), ['--version'], check=True)\n")
NONE = b"import unittest\n\n\nclass PlantedNone(unittest.TestCase):\n    def test_none(self):\n        pass\n"


def starts(path):
    """[(line, program)] of each call in a Python file that starts a
    process, by a name STARTS matches, its program as written (a list's
    first item, or the whole first argument); an import of such a call
    under another name is one too, its program unknown."""
    found = []
    for node in ast.walk(ast.parse(path.read_bytes())):
        if isinstance(node, ast.Call) and STARTS.fullmatch(ast.unparse(node.func)):
            first = node.args[0] if node.args else None
            if isinstance(first, (ast.List, ast.Tuple)) and first.elts:
                first = first.elts[0]
            found.append((node.lineno, ast.unparse(first) if first is not None else "?"))
        elif isinstance(node, ast.ImportFrom) and node.module in ("subprocess", "multiprocessing", "os", "pty"):
            found += [(node.lineno, "?") for a in node.names if STARTS.fullmatch(f"{node.module}.{a.name}")]
        elif isinstance(node, ast.Import):
            found += [(node.lineno, "?") for a in node.names if a.asname and a.name in ("subprocess", "os")]
    return sorted(found)


def cmake_true(value):
    """CMake's if() of a variable holding value, a preset's cacheVariables
    value (a string, a boolean, {"type", "value"}, None unset, an option's
    OFF) or a -D's: true unless a false constant; a value of bordering
    space, which this reading does not model, fails."""
    if isinstance(value, dict):
        value = value.get("value")
    if value is None or isinstance(value, bool):
        return bool(value)
    if not isinstance(value, str) or value != value.strip():
        raise AssertionError(f"a cache value this reading does not model: {value!r}")
    return not (value.upper() in FALSE or value.endswith("NOTFOUND"))


def inherited(table, name, key, field):
    """A preset's own field's key, else its parents' in order; None."""
    preset = table.get(name, {})
    if key in preset.get(field, {}):
        return preset[field][key]
    parents = preset.get("inherits", [])
    for parent in [parents] if isinstance(parents, str) else parents:
        got = inherited(table, parent, key, field)
        if got is not None:
            return got
    return None


def options(words, known):
    """[(option, its words)] of a command line after its program, each
    option in known with the words it takes, a -D's (NAME, value) by its
    -DNAME=V, -DNAME:TYPE=V or -D NAME=V; any other word fails."""
    out, i = [], 1
    while i < len(words):
        word = words[i]
        if word.startswith("-D") and "-D" in known:
            arg = word[2:] or (words[i + 1] if i + 1 < len(words) else "")
            i += 1 if word[2:] else 2
            found = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?::[A-Za-z]+)?=(.*)", arg, re.DOTALL)
            if not found:
                raise AssertionError(f"a -D this reading does not model: {arg!r} in {words}")
            out.append(("-D", found.groups()))
        elif any(k.endswith("=") and word.startswith(k) for k in known):
            out.append((word, ()))
            i += 1
        elif word in known and i + known[word] < len(words):
            out.append((word, tuple(words[i + 1:i + 1 + known[word]])))
            i += 1 + known[word]
        else:
            raise AssertionError(f"an option this reading does not model: {word!r} in {words}")
    return out


def configured(words, table, name):
    """The cache value a cmake --preset line gives name: the preset's,
    then each -D of it in order."""
    given = options(words, {**CMAKE_WORDS, "-D": 0})
    presets = [w[0] for option, w in given if option == "--preset"]
    value = inherited(table, presets[-1], name, "cacheVariables") if presets else None
    for option, got in given:
        if option == "-D" and got[0] == name:
            value = got[1]
    return value


def ctest_filters(words, table):
    """A ctest line's filters, {label_in: [regex], label_out: [regex],
    name_in: regex or None, name_out: regex or None}: its test preset's,
    each -L and -LE added to them, each -R and -E in place of the last."""
    given = options(words, CTEST_WORDS)
    presets = [w[0] for option, w in given if option == "--preset"]
    out = {"label_in": [], "label_out": [], "name_in": None, "name_out": None}
    if presets:
        for kind, side in (("include", "in"), ("exclude", "out")):
            got = inherited(table, presets[-1], kind, "filter") or {}
            if set(got) - {"label", "name"}:
                raise AssertionError(f"a filter this reading does not model: {got}")
            if "label" in got:
                out[f"label_{side}"].append(got["label"])
            out[f"name_{side}"] = got.get("name")
    for option, got in given:
        if option in FILTERS:
            kind = FILTERS[option]
            if kind.startswith("label"):
                out[kind].append(got[0])
            else:
                out[kind] = got[0]
    return out


def selects(filters, labels, test_id):
    """Whether a ctest step of these filters runs the test, by search as
    ctest matches: every -L matching a label, not every -LE, the -R the
    name and not the -E."""
    def hit(pattern):
        return any(re.search(pattern, label) for label in labels)

    return (all(hit(p) for p in filters["label_in"]) and not (filters["label_out"] and
                                                              all(hit(p) for p in filters["label_out"]))
            and (filters["name_in"] is None or re.search(filters["name_in"], test_id))
            and not (filters["name_out"] and re.search(filters["name_out"], test_id)))


@functools.cache
def git_tests():
    """The loop's own tests, as the loop's build lists them: (ids, their
    labels as ctest holds them)."""
    rows, errors = runpy.run_path(str(ROOT / "tests" / "runner" / "list_tests.py"))["listed"](True, ROOT)
    if errors:
        raise AssertionError("\n".join(errors))
    ids = sorted(row[0] for row in rows if "loop" in row[1])
    labels = {("py", *row[1]) for row in rows if "loop" in row[1]}
    if len(labels) != 1:
        raise AssertionError(f"the loop's own tests' labels are not one set: {labels}")
    return ids, set(labels.pop())


def loop_steps(loop, way, system, data=None, extra=None):
    """The ctest steps of the way's plan on the system that run one of the
    loop's own tests: [(unit, step)]. data, CMakePresets.json's in its
    place; extra, {(unit, step): words} added to those steps' lines."""
    data = data or json.loads((ROOT / "CMakePresets.json").read_bytes())
    configure = {p["name"]: p for p in data["configurePresets"]}
    test_presets = {p["name"]: p for p in data["testPresets"]}
    presets, directory = loop.binary_dirs(ROOT), loop.TIERS["directory"]
    ids, labels = git_tests()
    units = loop.plan(way, system, [], None, ROOT)
    registered, found = {}, []
    for unit in units:
        for step in unit.steps:
            words = tuple(step.ci) + tuple((extra or {}).get((unit.name, step.name), ()))
            if words[:2] == ("cmake", "--preset"):
                tree = directory(step, presets)
                registered[unit.map.get(tree, tree)] = (
                    cmake_true(configured(words, configure, "QIVEN3_LOOP_TESTS"))
                    and not cmake_true(configured(words, configure, "QIVEN3_STRUCTURE")))
    for unit in units:
        for step in unit.steps:
            words = tuple(step.ci) + tuple((extra or {}).get((unit.name, step.name), ()))
            if words[0] != "ctest":
                continue
            tree = directory(step, presets)
            tree = unit.map.get(tree, tree)
            if tree not in registered:
                raise AssertionError(f"{unit.name} {step.name}: no configure of {tree} in the plan")
            filters = ctest_filters(words, test_presets)
            if registered[tree] and any(selects(filters, labels, i) for i in ids):
                found.append((unit.name, step.name))
    return found


class Shim(unittest.TestCase):
    """The shim's run of a planted test in a scratch tree: its starts
    counted in the enclosing count, its faults."""
    ctest_labels = ("fake",)
    ctest_git = True  # its plants start git: listed on the loop's build alone

    def setUp(self):
        if sys.platform == "win32" and not shutil.which("cl"):
            self.skipTest("no cl on PATH: the shim is built by the toolset's cl, under vcvars")
        self.top = Path(tempfile.mkdtemp(prefix="qiven3-git-shim-"))
        self.addCleanup(shutil.rmtree, self.top, ignore_errors=True)
        self.root = self.top / "root"
        for package in ("tests", "tests/drive", "tools"):
            (self.root / package).mkdir(parents=True)
            (self.root / package / "__init__.py").write_bytes(b"")
        shutil.copyfile(ROOT / "tools" / "loop_git.py", self.root / "tools" / "loop_git.py")

    def shimmed(self, module, text, test_id, git):
        """The planted module run behind the shim, --git if git: (status,
        standard error, the starts the enclosing count gained)."""
        (self.root / "tests" / "drive" / module).write_bytes(text)
        outer = self.top / f"outer-{module}-{git}"
        env = {k: v for k, v in os.environ.items() if k not in (loop_git.FAKE, loop_git.COUNT)}
        env[loop_git.COUNT] = str(outer)
        done = subprocess.run([sys.executable, "-I", str(SHIM), *(["--git"] if git else []), "--", sys.executable,
                               "-I", "-m", "unittest", "discover", "-s", "tests", "-t", ".", "-k", "*" + test_id],
                              cwd=self.root, env=env, stdin=subprocess.DEVNULL, capture_output=True, timeout=300)
        gained = loop_git.verdict(outer)[0]
        loop_git.carried(gained, None)  # this test's own starts, the enclosing shim's too
        return done.returncode, done.stderr.decode("utf-8", "replace"), gained

    def test_outside(self):
        """A start by a name built at run time and one by a shell's line,
        past the door: both counted, the run failing as past the door,
        and as no loop's test without --git."""
        for git in (False, True):
            with self.subTest(git=git):
                status, err, gained = self.shimmed("test_planted_outside.py", OUTSIDE,
                                                   "tests.drive.test_planted_outside.PlantedOutside.test_outside", git)
                self.assertEqual((status, gained), (1, 2), err)
                self.assertIn("the shim's 2 starts against the door's 0: a start past the door", err)
                self.assertEqual("not the loop's own" in err, not git, err)

    def test_door_named_or_by_runpy(self):
        """A module named test*.py, not test_*.py, starting git through the
        door, and one reaching the door by runpy: counted, failing unless
        the loop's own."""
        for module, text, test_id, modes in (
                ("testplant.py", NAMED, "tests.drive.testplant.PlantedName.test_door", (False, True)),
                ("test_planted_runpy.py", BY_RUNPY, "tests.drive.test_planted_runpy.PlantedRunpy.test_door", (False,))):
            for git in modes:
                with self.subTest(module=module, git=git):
                    status, err, gained = self.shimmed(module, text, test_id, git)
                    self.assertEqual((status, gained), (0 if git else 1, 1), err)
                    self.assertNotIn("past the door", err)
                    self.assertEqual("1 starts of git by a test that is not the loop's own" in err, not git, err)

    def test_none(self):
        """A test that starts no git passes, nothing counted."""
        status, err, gained = self.shimmed("test_planted_none.py", NONE,
                                           "tests.drive.test_planted_none.PlantedNone.test_none", False)
        self.assertEqual((status, gained), (0, 0), err)


class Plan(unittest.TestCase):
    """The tiers' plans read as CMake and CTest read them."""
    ctest_labels = ("fake",)

    def test_cmake_true(self):
        """CMake's reading, as measured: a true constant, a number not 0, any
        other string; a false constant in any case, NOTFOUND's suffix in its
        own; a preset's boolean and object; unset, the option's OFF."""
        for value in ("ON", "on", "On", "1", "2", "-1", "0.5", "0.0", "00", "1e3", "TRUE", "true", "YES", "y", "Y",
                      "abc", "0x1", "x-notfound", "Notfound", True, {"type": "BOOL", "value": "ON"}):
            self.assertTrue(cmake_true(value), value)
        for value in ("N", "n", "NO", "no", "nO", "OFF", "off", "FALSE", "False", "0", "IGNORE", "ignore",
                      "NOTFOUND", "X-NOTFOUND", "x-NOTFOUND", "-NOTFOUND", "", None, False,
                      {"type": "BOOL", "value": False}):
            self.assertFalse(cmake_true(value), value)
        for value in (" ON", "OFF ", 2):
            self.assertRaises(AssertionError, cmake_true, value)

    def test_configured(self):
        """A -D in each form, the last one standing, over the preset's."""
        table = {"p": {"cacheVariables": {"X": "ON"}}, "q": {"inherits": ["p"], "cacheVariables": {"X": None}}}
        for words, value in ((("cmake", "--preset", "p"), "ON"), (("cmake", "--preset", "q"), None),
                             (("cmake", "--preset", "p", "-DX=OFF"), "OFF"),
                             (("cmake", "--preset", "p", "-DX:BOOL=2"), "2"),
                             (("cmake", "--preset", "q", "-D", "X=yes"), "yes"),
                             (("cmake", "--preset", "p", "-B", "b", "-DX=OFF", "-D", "X:STRING=ON"), "ON"),
                             (("cmake", "--preset", "p", "-DY=OFF"), "ON")):
            self.assertEqual(configured(words, table, "X"), value, words)
        for words in (("cmake", "--preset", "p", "-UX"), ("cmake", "--preset", "p", "-D", "=1")):
            self.assertRaises(AssertionError, configured, words, table, "X")

    def test_ctest_filters(self):
        """Each filter form against what CTest ran in a scratch project of
        the same tests and presets."""
        tests = {"t.a": {"py", "fake"}, "t.b": {"py", "loop", "fake"}, "t.c": {"fast"}, "s.x": {"loop"},
                 "s.y": {"py", "drive"}}
        table = {"fake": {"filter": {"include": {"label": "^fake$"}}},
                 "named": {"filter": {"include": {"name": "^t[.]"}, "exclude": {"name": "c$"}}},
                 "nopy": {"filter": {"exclude": {"label": "^py$"}}}}
        measured = (("-L py", "t.a t.b s.y"), ("-L py -L loop", "t.b"), ("-L loop -L py", "t.b"),
                    ("-LE py", "t.c s.x"), ("-LE py -LE loop", "t.a t.c s.x s.y"), ("-R a$", "t.a"),
                    ("-R a$ -R b$", "t.b"), ("-R b$ -R a$", "t.a"), ("-E a$", "t.b t.c s.x s.y"),
                    ("-E a$ -E b$", "t.a t.c s.x s.y"), ("--label-regex loop", "t.b s.x"),
                    ("--label-exclude fake", "t.c s.x s.y"), ("--tests-regex ^s", "s.x s.y"),
                    ("--exclude-regex ^s", "t.a t.b t.c"), ("-L ^loop$ -R ^t -E a$", "t.b"),
                    ("--preset fake", "t.a t.b"), ("--preset fake -L loop", "t.b"), ("--preset fake -LE loop", "t.a"),
                    ("--preset fake -R a$", "t.a"), ("--preset named", "t.a t.b"), ("--preset named -R ^s", "s.x s.y"),
                    ("--preset named -E a$", "t.b t.c"), ("--preset nopy", "t.c s.x"),
                    ("--preset nopy -LE ^loop$", "t.a t.c s.x s.y"), ("--preset nopy -L loop", "s.x"),
                    ("-L py|fast", "t.a t.b t.c s.y"), ("-R A$", ""), ("-L LOOP", ""))
        for line, names in measured:
            with self.subTest(line=line):
                filters = ctest_filters(("ctest", *line.split()), table)
                self.assertEqual(" ".join(n for n, labels in tests.items() if selects(filters, labels, n)), names)
        for line in ("-I 1,2", "-U", "-L", "--label-regex=x", "-j 4"):
            self.assertRaises(AssertionError, ctest_filters, ("ctest", *line.split()), table)

    def test_one_step_a_run(self):
        """Each tier's plan on each system, tier 2's after a change to the
        loop's tools (which runs every test: the same plan), runs the
        loop's own tests, and so their git, in one ctest step: the steps'
        bounds fit the run's."""
        loop = load_loop()
        for way in ("1", "2", "3"):
            for system in ("windows", "linux"):
                with self.subTest(way=way, system=system):
                    found = loop_steps(loop, way, system)
                    self.assertEqual(found, [(loop.TABLE["TIER1"][system], "fake")])
                    self.assertLessEqual(len(found) * loop_git.LIMIT, loop_git.RUN_LIMIT)

    def test_planted_plans(self):
        """The option turned on in a second build, by a preset's object, a
        number or a -D of each form, and the structure tree's records
        turned off: a second step found; the last -D standing."""
        loop = load_loop()
        data = json.loads((ROOT / "CMakePresets.json").read_bytes())
        asan = next(p for p in data["configurePresets"] if p["name"] == "ci-asan")
        for value in ({"type": "BOOL", "value": "ON"}, "2", "yes", True):
            with self.subTest(value=value):
                asan["cacheVariables"]["QIVEN3_LOOP_TESTS"] = value
                self.assertEqual(len(loop_steps(loop, "2", "windows", data)), 2)
        asan["cacheVariables"]["QIVEN3_LOOP_TESTS"] = "OFF"
        configure = ("windows-asan", "configure")
        for words in (("-DQIVEN3_LOOP_TESTS:BOOL=ON",), ("-D", "QIVEN3_LOOP_TESTS=y"),
                      ("-DQIVEN3_LOOP_TESTS=OFF", "-DQIVEN3_LOOP_TESTS=1")):
            with self.subTest(words=words):
                self.assertEqual(len(loop_steps(loop, "3", "windows", data, {configure: words})), 2)
        self.assertEqual(len(loop_steps(loop, "2", "windows", data, {
            configure: ("-DQIVEN3_LOOP_TESTS=ON", "-DQIVEN3_LOOP_TESTS=OFF")})), 1)
        structure = {("windows-analysis", "structure configure"): ("-DQIVEN3_STRUCTURE=OFF",)}
        self.assertEqual(loop_steps(loop, "2", "windows", data, structure),
                         [("windows-msvc", "fake"), ("windows-analysis", "structure tests")])
        linux = {("linux-plain", "configure"): ("-DQIVEN3_LOOP_TESTS=ON",)}
        self.assertEqual(len(loop_steps(loop, "3", "linux", data, linux)), 3)


class Exempt(unittest.TestCase):
    """The guard's exemptions against the processes their files start."""
    ctest_labels = ("fake",)

    def test_exempt_start_none(self):
        """Each file the guard exempts is the door or starts no process
        but those it names, which are not git."""
        own = set(tests.drive.test_git_count.OWN)
        self.assertEqual(own - set(STARTING_NO_GIT), {"tools/loop_git.py"})
        for rel in sorted(STARTING_NO_GIT):
            with self.subTest(rel=rel):
                self.assertIn(rel, own)
                self.assertLessEqual({program for _, program in starts(ROOT / rel)}, STARTING_NO_GIT[rel],
                                     starts(ROOT / rel))

    def test_starts_read(self):
        """Each form of a start the exemptions' check reads, planted, is
        read; a call that starts nothing is not."""
        top = Path(tempfile.mkdtemp(prefix="qiven3-git-starts-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        plant = top / "plant.py"
        plant.write_bytes(b"subprocess.run([prog, 'x'])\nos.system(line)\nos.execvp(p, a)\n"
                          b"from subprocess import Popen\nimport subprocess as s\nmultiprocessing.Process()\n"
                          b"os.path.join(a)\nprint(subprocess)\n")
        self.assertEqual(starts(plant), [(1, "prog"), (2, "line"), (3, "p"), (4, "?"), (5, "?"), (6, "?")])


if __name__ == "__main__":
    unittest.main()
