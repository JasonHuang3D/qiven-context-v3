"""The loop's own tests run here alone, each group in a child process with a
counting shim placed first on PATH under git's program name, which logs
each start and runs the real git (tools/git_shim.c, built here by the
toolset's cl, on Windows; tools/git_shim.sh on Linux): those that start
git (test_loop, test_loop_git, test_line_endings), those that run on the
fake (test_loop_pins, test_loop_select, test_nav), and those that reach the
loop's tools and start none (test_git_count and the loop's tables). The
listing registers no group's module as tests of their own, and this
module's tests on one build a system alone (tests/runner/list_tests.py,
QIVEN3_LOOP_TESTS), so that the git the loop's tests start is started in
one step a run. A group fails when its tests fail, or when the shim's
starts differ from the door's (tools/loop_git.py) for the same run: a start
past the door, by a name the guard reads (tests/drive/test_git_count.py) or
a program held in a variable, which no reading of the source sees.

Cover checks that every test module whose imports reach the loop's tools
is a group's; that each file the guard exempts is a group's, the door, or
starts no process but those it names; and, from each tier's plan on each
system, that one ctest step a run registers and selects this module's
tests, so that the steps' bounds fit the run's.

The child's starts are counted in a directory of the group's own, for the
comparison (ctest runs the groups at once), then added to the enclosing
step's count (loop_git.carried): the step's count is its real starts. The
real git is the door's, found before the shim is placed; the shim's log is
a file this test makes, never under HOME, which test_loop_git changes.
Each group stays under about 15 seconds on a CI Windows job: test_loop runs
by its classes, test_nav by its cases' cost, a case of clangd's index alone
or with a short one. A group's class declares what its tests read
(ctest_reads), with the shim's sources; this module imports every group's,
so that a change to one selects its groups."""

import ast
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

# Each group's module, imported so that a change to one selects the groups.
import tests.drive.test_git_count
import tests.drive.test_loop
import tests.drive.test_loop_answer
import tests.drive.test_loop_git
import tests.drive.test_loop_pins
import tests.drive.test_loop_select
import tests.drive.test_nav
import tests.drive.test_tier_one
import tests.structure.test_line_endings
from tests.drive.test_loop_ci import ROOT, load_loop
from tools import loop_git

# Each group of the loop's tests, as unittest names them.
GROUPS = {
    "loop_git": ("tests.drive.test_loop_git",),
    "fake": ("tests.drive.test_loop_pins", "tests.drive.test_loop_select"),
    "nav_lookups": ("tests.drive.test_nav.Nav.test_references", "tests.drive.test_nav.Nav.test_unresolved"),
    "nav_truncated": ("tests.drive.test_nav.Nav.test_truncated",),
    "nav_types": ("tests.drive.test_nav.Nav.test_type_and_refusals",),
    "nav_index_used": ("tests.drive.test_nav.Nav.test_index_used",),
    "nav_index_stale_file": ("tests.drive.test_nav.Nav.test_index_stale_file", "tests.drive.test_nav.Checkout"),
    "nav_index_stale_clangd": ("tests.drive.test_nav.Nav.test_index_stale_clangd",),
    "loop": tuple(f"tests.drive.test_loop.{c}" for c in ("Answer", "Arguments", "Tiers", "OneClone")),
    "loop_windows": ("tests.drive.test_loop.Windows",),
    "loop_linux": ("tests.drive.test_loop.Linux",),
    "line_endings": ("tests.structure.test_line_endings",),
    "git_count": ("tests.drive.test_git_count",),
    "loop_tables": ("tests.drive.test_loop_answer", "tests.drive.test_loop_ci", "tests.drive.test_tier_one"),
}
ALONE = ("loop_windows", "loop_linux")  # the groups of test_loop's classes that run alone, alone as they
SHIM_READS = ("tools/git_shim.c", "tools/git_shim.sh")
RUN = ("import sys, unittest; sys.path.insert(0, sys.argv[1]); "
       "unittest.main(module=None, argv=['unittest', *sys.argv[2:]])")
SHIM_LOG, SHIM_REAL = "QIVEN3_SHIM_LOG", "QIVEN3_SHIM_REAL"
# The loop's tools: a test module whose imports reach one runs in a group.
LOOP_TOOLS = {"tools/loop.py", "tools/loop_git.py", "tools/loop_run.py"}
# A call that starts a process, by its dotted name; and the files the guard
# exempts that are no group's module and not the door, with the programs
# their calls may start.
STARTS = re.compile(r"(?:subprocess|multiprocessing|pty)\.\w+|os\.(?:system|popen|exec\w*|spawn\w*|posix_spawn\w*|"
                    r"startfile|fork\w*)")
STARTING_NO_GIT = {"tests/structure/rules.py": set(), "tests/drive/test_git_shim.py": {"cl", "sys.executable"}}
ON = {"ON", "1", "TRUE", "YES", "Y"}


def build_shim(top):
    """The shim's directory, its program there under git's name; None, with
    why, when it cannot be made."""
    shim = top / "bin"
    shim.mkdir()
    if sys.platform != "win32":
        target = shim / "git"
        shutil.copyfile(ROOT / "tools" / "git_shim.sh", target)
        target.chmod(0o755)
        return shim, None
    cl = shutil.which("cl")
    if not cl:
        return None, "no cl on PATH: the shim is built by the toolset's cl, under vcvars"
    target = shim / "git.exe"
    done = subprocess.run([cl, "/nologo", "/W4", "/WX", "/O1", str(ROOT / "tools" / "git_shim.c"), f"/Fe{target}",
                           f"/Fo{top}\\", "kernel32.lib"], cwd=top, capture_output=True, stdin=subprocess.DEVNULL,
                          timeout=300)
    if done.returncode != 0 or not target.is_file():
        raise AssertionError("the shim did not build: " + done.stdout.decode("utf-8", "replace")[-2000:])
    return shim, None


def loaded(names):
    """The tests unittest loads from names, by a loader of their own (the
    default one holds the running test's name patterns)."""
    def walk(suite):
        for test in suite:
            yield from walk(test) if isinstance(test, unittest.TestSuite) else (test,)

    return list(walk(unittest.TestLoader().loadTestsFromNames(names)))


def ids(names):
    return {test.id() for test in loaded(names)}


def modules():
    """The groups' modules, as unittest names them: the listing registers
    none of their tests (tests/runner/list_tests.py)."""
    return {".".join(n.split(".")[:3]) for names in GROUPS.values() for n in names}


class Groups(unittest.TestCase):
    """The shim, built once a class, and a group's run under it; no test of
    its own."""
    ctest_labels = ("fake",)
    ctest_reads = SHIM_READS

    @classmethod
    def setUpClass(cls):
        cls.top = Path(tempfile.mkdtemp(prefix="qiven3-git-shim-"))
        cls.real = loop_git.which() if not os.environ.get(loop_git.FAKE) else None
        cls.shim, cls.why = build_shim(cls.top)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.top, ignore_errors=True)

    def group(self, name):
        """The group run under the shim: its tests pass, and the shim's
        starts are the door's, which the enclosing step counts."""
        if self.shim is None:
            self.skipTest(self.why)
        self.assertTrue(self.real and not str(self.real).endswith(".py"), self.real)
        top = self.top / name
        top.mkdir()
        log, count = top / "shim.log", top / "count"
        log.write_bytes(b"")
        env = {k: v for k, v in os.environ.items() if k not in (loop_git.FAKE, loop_git.COUNT)}
        env.update({"PATH": str(self.shim) + os.pathsep + env.get("PATH", ""), loop_git.COUNT: str(count),
                    SHIM_LOG: str(log), SHIM_REAL: str(self.real)})
        done = subprocess.run([sys.executable, "-I", "-c", RUN, str(ROOT), *GROUPS[name]], cwd=ROOT, env=env,
                              stdin=subprocess.DEVNULL, capture_output=True, timeout=600)
        starts = log.read_text(encoding="utf-8", errors="replace").splitlines()
        loop_git.carried(len(starts), None)
        text = done.stderr.decode("utf-8", "replace")
        self.assertEqual(done.returncode, 0, text[-3000:])
        door = loop_git.verdict(count)[0]
        self.assertEqual(len(starts), door, "the shim's starts against the door's:\n" + "\n".join(starts[-60:]))
        return door, [line for line in text.splitlines() if line.startswith(("Ran ", "OK", "FAILED"))]


def shim_class(name):
    """The class of a group's test: what the group's tests read, with the
    shim's sources, and alone where the group is."""
    def test(self):
        self.group(name)

    reads = dict.fromkeys(SHIM_READS)
    for case in loaded(GROUPS[name]):
        reads.update(dict.fromkeys(getattr(case, "ctest_reads", ())))
    title = "Shim" + "".join(part.title() for part in name.split("_"))
    return title, type(title, (Groups,), {"__doc__": f"The group {name} under the shim.", "ctest_reads": tuple(reads),
                                          "ctest_serial": name in ALONE, f"test_{name}": test})


globals().update(shim_class(name) for name in GROUPS)


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


def selects(filters, labels, test_id):
    """Whether a ctest step of these filters, (label include, label
    exclude, name include, name exclude) each a regex or None, runs the
    test: by search, as ctest matches them."""
    include, exclude, named, unnamed = filters

    def hit(pattern):
        return any(re.search(pattern, label) for label in labels)

    return ((include is None or hit(include)) and not (exclude and hit(exclude))
            and (named is None or re.search(named, test_id)) and not (unnamed and re.search(unnamed, test_id)))


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


def loop_steps(loop, way, system):
    """The ctest steps of the way's plan on the system that run a test of
    this module: [(unit, step)]. A step's tree registers them when the
    configure that wrote it, in the same plan, sets QIVEN3_LOOP_TESTS
    (CMakePresets.json, or its own -D); the step runs one when its test
    preset's filter, its own -L, -LE, -R and -E over it, selects it."""
    data = json.loads((ROOT / "CMakePresets.json").read_bytes())
    configure = {p["name"]: p for p in data["configurePresets"]}
    test_presets = {p["name"]: p for p in data["testPresets"]}
    presets, directory = loop.binary_dirs(ROOT), loop.TIERS["directory"]
    units = loop.plan(way, system, [], None, ROOT)
    shim = sorted(ids([__name__]))
    labels = {"py", "drive", *Groups.ctest_labels}
    registered, found = {}, []
    for unit in units:
        for step in unit.steps:
            words = step.ci
            if words[:2] == ("cmake", "--preset"):
                value = inherited(configure, words[2], "QIVEN3_LOOP_TESTS", "cacheVariables")
                for word in words:
                    if word.startswith("-DQIVEN3_LOOP_TESTS="):
                        value = word.split("=", 1)[1]
                tree = directory(step, presets)
                registered[unit.map.get(tree, tree)] = str(value).upper() in ON
    for unit in units:
        for step in unit.steps:
            words = list(step.ci)
            if words[0] != "ctest":
                continue
            tree = directory(step, presets)
            tree = unit.map.get(tree, tree)
            if tree not in registered:
                raise AssertionError(f"{unit.name} {step.name}: no configure of {tree} in the plan")
            preset = words[words.index("--preset") + 1] if "--preset" in words else None
            filters = []
            for option, kind, key in (("-L", "include", "label"), ("-LE", "exclude", "label"),
                                      ("-R", "include", "name"), ("-E", "exclude", "name")):
                value = (inherited(test_presets, preset, kind, "filter") or {}).get(key) if preset else None
                filters.append(words[words.index(option) + 1] if option in words else value)
            if registered[tree] and any(selects(filters, labels, i) for i in shim):
                found.append((unit.name, step.name))
    return found


class Cover(unittest.TestCase):
    """The groups against the modules, the guard's exemptions and the
    tiers' plans."""
    ctest_labels = ("fake",)
    ctest_reads = ("tests/",)

    def test_groups_cover(self):
        """The groups hold every test of their modules, each once, and
        every test module whose imports reach the loop's tools is a
        group's, this one apart."""
        per_group = [ids(names) for names in GROUPS.values()]
        self.assertEqual(sum(len(g) for g in per_group), len(set().union(*per_group)))
        self.assertEqual(set().union(*per_group), ids(sorted(modules())))
        imports = load_loop().SELECT["Imports"](ROOT)
        reaching = set()
        for path in sorted((ROOT / "tests").rglob("test_*.py")):
            rel = path.relative_to(ROOT).as_posix()
            if imports.closure(rel) & LOOP_TOOLS:
                reaching.add(rel.removesuffix(".py").replace("/", "."))
        self.assertIn(__name__, reaching)
        self.assertEqual(sorted(reaching - modules() - {__name__}), [])

    def test_exempt_start_none(self):
        """Each file the guard exempts is a group's module, the door, which
        every group's imports reach, or starts no process but those it
        names, which are not git."""
        imports = load_loop().SELECT["Imports"](ROOT)
        reached = set().union(*(imports.closure(m.replace(".", "/") + ".py") for m in modules()))
        for rel in tests.drive.test_git_count.OWN:
            with self.subTest(rel=rel):
                if rel.removesuffix(".py").replace("/", ".") in modules():
                    continue
                if rel == "tools/loop_git.py":
                    self.assertIn(rel, reached)
                    continue
                self.assertIn(rel, STARTING_NO_GIT)
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

    def test_one_step_a_run(self):
        """Each tier's plan on each system, tier 2's after a change to the
        loop's tools (which runs every test: the same plan), runs this
        module's tests, and so the loop's git, in one ctest step: the
        steps' bounds fit the run's."""
        loop = load_loop()
        for way in ("1", "2", "3"):
            for system in ("windows", "linux"):
                with self.subTest(way=way, system=system):
                    found = loop_steps(loop, way, system)
                    self.assertEqual(len(found), 1, found)
                    self.assertLessEqual(len(found) * loop_git.LIMIT, loop_git.RUN_LIMIT)
                    self.assertEqual(found[0], (loop.TABLE["TIER1"][system], "fake"))


if __name__ == "__main__":
    unittest.main()
