"""The loop's tiers on fakes (tools/loop.py): what each runs, each
configure's --fresh in tier 3 alone, a job's tree kept through another
job's run and its own next one, tier 3 emptying it with the fuzz corpus
kept, a tree outside build/ never emptied. Linux's one clone on fakes:
synced once a run, a sync that changes nothing rewriting no file, a build
tree a job within it, tier 3 emptying those trees alone, the suites
reading the jobs' trees."""

import argparse
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

from tests.drive.own_git import git, own_git
from tests.drive.test_loop import LOOP, Loop
from tests.drive.test_loop_ci import ROOT
from tools import loop_git


class Tiers(Loop):
    """The ways on fakes: what each runs, and the kept trees, each step a
    fake command recording its argv in the job's tree."""

    def plan(self, way, system="windows", fuzz=None):
        jobs = [n for n, job in LOOP.JOBS.items() if job[0] == system]
        return {u.name: u for u in LOOP.plan(way, system, jobs, fuzz)}

    def argv(self, unit, name):
        presets = LOOP.binary_dirs()
        return [LOOP.local_argv(unit, s, presets) for s in unit.steps if s.name == name][0]

    def test_ways(self):
        windows = [n for n, job in LOOP.JOBS.items() if job[0] == "windows"]
        one = self.plan("1")
        self.assertEqual(list(one), ["windows-msvc"])
        steps = [s.name for s in one["windows-msvc"].steps]
        # Tier 1 builds no structure tree: its steps are the job's, fast and size.
        self.assertEqual(steps, [*(s.name for s in LOOP.JOBS["windows-msvc"][2]), "fast", "size"])
        self.assertNotIn("structure", " ".join(steps))
        self.assertEqual(one["windows-msvc"].trees, ["build/windows-msvc"])
        self.assertEqual(self.argv(one["windows-msvc"], "fast")[:3], ["ctest", "--test-dir", "build/windows-msvc"])
        self.assertEqual(list(self.plan("2")), windows)
        three = self.plan("3")
        self.assertEqual(list(three), [*windows, "windows-full", "windows-fuzz"])
        self.assertEqual(self.argv(three["windows-fuzz"], "fuzz"), ["ctest", "--preset", "fuzz"])
        timed = self.plan("3", fuzz=600)["windows-fuzz"]
        self.assertEqual(self.argv(timed, "fuzz"), [LOOP.TABLE["FUZZ_TIMED"], "build/fuzz", "600"])
        self.assertEqual([s.timeout for s in timed.steps if s.name == "fuzz"], [0])
        self.assertEqual(list(self.plan("1", "linux")), ["linux-gcc"])
        self.assertEqual(self.plan("1", "linux")["linux-gcc"].steps[-1].name, "fast")
        # Linux's fuzz targets run once: untimed in linux-fuzz's own step
        # alone, timed in their suite.
        linux = [n for n, job in LOOP.JOBS.items() if job[0] == "linux"]
        self.assertEqual(list(self.plan("3", "linux")), [*linux, "linux-full"])
        self.assertIn("fuzz", [s.name for s in self.plan("3", "linux")["linux-fuzz"].steps])
        timed = self.plan("3", "linux", fuzz=600)
        self.assertEqual(list(timed), [*linux, "linux-full", "linux-fuzz-targets"])
        self.assertEqual(self.argv(timed["linux-fuzz-targets"], "fuzz"),
                         [LOOP.TABLE["FUZZ_TIMED"], "build/linux-fuzz", "600"])

    def test_fresh(self):
        """Every configure has --fresh in tier 3 and in no other way."""
        for way in ("1", "2", "3", "test"):
            for unit in self.plan(way).values():
                for step in unit.steps:
                    if step.ci[:2] == ("cmake", "--preset"):
                        with self.subTest(way=way, job=unit.name, step=step.name):
                            self.assertEqual("--fresh" in self.argv(unit, step.name), way == "3")

    def test_own_trees(self):
        """Each job's commands in its own trees: none in CI's shared build/ci."""
        for unit in [*self.plan("1").values(), *self.plan("3").values()]:
            for step in unit.steps:
                with self.subTest(job=unit.name, step=step.name):
                    self.assertNotIn("build/ci", self.argv(unit, step.name))
        msvc = self.plan("2")["windows-msvc"]
        self.assertEqual(self.argv(msvc, "configure")[:5], ["cmake", "--preset", "ci-windows", "-B",
                                                            "build/windows-msvc"])
        self.assertEqual(self.argv(msvc, "build")[:3], ["cmake", "--build", "build/windows-msvc"])
        self.assertEqual(self.argv(msvc, "fake"), ["ctest", "--preset", "fake", "--test-dir", "build/windows-msvc"])
        self.assertEqual(self.argv(msvc, "clean binaries"), [LOOP.CLEAN, "build/windows-msvc"])

    def drive(self, root, units):
        """The units run by fake commands: each appends its argv to ran.txt
        in the unit's first tree."""
        presets = LOOP.binary_dirs()
        code = "import json, sys; open(sys.argv[1], 'a').write(json.dumps(sys.argv[2:]) + '\\n')"

        def command(unit, step):
            return [sys.executable, "-I", "-c", code, str(root / unit.trees[0] / "ran.txt"),
                    *LOOP.local_argv(unit, step, presets)]

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            counts = LOOP.drive(LOOP.Report(LOOP.LINES), units, root, root / "build" / "loop", command,
                                dict(os.environ))
        self.assertEqual(counts[1:], [0, 0], out.getvalue())
        return out.getvalue()

    def ran(self, root, tree):
        path = root / tree / "ran.txt"
        return [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines()] if path.is_file() else []

    def test_kept_trees(self):
        """A job's tree survives another job's run and its own next run;
        tier 3 empties it, the fuzz corpus kept, and configures --fresh; a
        tree another job left is emptied."""
        root = Path(tempfile.mkdtemp(prefix="qiven3-loop-trees-"))
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        two, three = self.plan("2"), self.plan("3")
        msvc, asan = "build/windows-msvc", "build/windows-asan"
        self.drive(root, [two["windows-msvc"]])
        self.drive(root, [two["windows-asan"]])
        steps = len(two["windows-msvc"].steps)
        self.assertEqual(len(self.ran(root, msvc)), steps)
        out = self.drive(root, [two["windows-msvc"]])
        self.assertIn(f"windows-msvc tree: {root / msvc} kept, its own", out)
        self.assertEqual(len(self.ran(root, msvc)), 2 * steps)
        self.assertFalse(any("--fresh" in argv for argv in self.ran(root, msvc)))
        seed = root / msvc / "fuzz" / "t" / "corpus" / "seed"
        seed.parent.mkdir(parents=True)
        seed.write_bytes(b"kept")
        (root / msvc / "fuzz" / "t" / "artifacts").mkdir()
        out = self.drive(root, [three["windows-msvc"]])
        self.assertIn(f"windows-msvc tree: {root / msvc} emptied, a fresh run, 1 corpus directories kept", out)
        ran = self.ran(root, msvc)
        self.assertEqual(len(ran), len(three["windows-msvc"].steps))  # tier 2's drive step not tier 3's
        self.assertIn("--fresh", ran[0])
        self.assertEqual(seed.read_bytes(), b"kept")
        self.assertFalse((root / msvc / "fuzz" / "t" / "artifacts").exists())
        self.assertFalse((root / "build" / "windows-msvc.kept").exists())
        self.assertEqual(len(self.ran(root, asan)), len(two["windows-asan"].steps))
        (root / asan / LOOP.RUN["TREE_MARK"]).write_text("windows-stack\n", encoding="utf-8")
        out = self.drive(root, [two["windows-asan"]])
        self.assertIn("emptied, it held windows-stack", out)
        self.assertEqual(len(self.ran(root, asan)), len(two["windows-asan"].steps))

    def test_empty_refused(self):
        """No tree outside the root's build/ is emptied, nor build/ itself."""
        root = Path(tempfile.mkdtemp(prefix="qiven3-loop-trees-"))
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        for tree in (root / "elsewhere", root / "build", root / "build" / ".." / "src"):
            tree.mkdir(parents=True, exist_ok=True)
            (tree / "file").write_bytes(b"x")
            with self.subTest(tree=tree), self.assertRaises(LOOP.CannotRun):
                LOOP.RUN["empty"](root, tree)
            self.assertTrue((tree / "file").is_file())


class OneClone(Loop):
    """Linux's one clone on fakes, on this system's git in an environment of
    the case's own (tests/drive/own_git.py): a source repository in a
    scratch directory, synced into the clone root's clone/; each job's build
    tree within it; tier 3 emptying the build trees alone."""

    REAL = ("test_sync",)  # git's own property: its fetch, checkout and clean; the rest on the fake

    def setUp(self):
        top = Path(tempfile.mkdtemp(prefix="qiven3-loop-clone-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        own_git(self, top, fake=self._testMethodName not in self.REAL)  # no host config read (969)
        self.git = loop_git.which()
        if not self.git:
            self.skipTest("git is missing from PATH")
        self.source, self.base = top / "source", top / "root"
        self.source.mkdir()
        self.fixture_git("init", "-q")
        (self.source / "a.txt").write_bytes(b"one\n")
        for argv in (("add", "-A"), ("commit", "-q", "-m", "a commit")):
            self.fixture_git(*argv)
        self.first = self.fixture_git("rev-parse", "HEAD")
        self.old = self.base / "linux-gcc"  # a clone an earlier loop kept for a job
        (self.old / ".git").mkdir(parents=True)
        (self.old / "kept.txt").write_bytes(b"old")
        self.linux = LOOP.LINUX["inside"].__globals__

    def fixture_git(self, *argv):
        return git(self.source, *argv)

    def test_sync(self):
        """A sync that changes nothing rewrites no file and keeps build/; the
        clone's untracked files go; the snapshot's new file arrives, the
        source's index untouched, under the checkout's .gitattributes a text
        file's CRLF stored and checked out as LF, a -text one's kept; the
        earlier clones are named, untouched."""
        clone = self.base / self.linux["CLONE"]
        self.linux["sync"](self.git, clone, str(self.source), self.first)
        (clone / "build" / "linux-gcc").mkdir(parents=True)
        (clone / "build" / "linux-gcc" / "object.o").write_bytes(b"built")
        (clone / "stray.txt").write_bytes(b"x")
        before = os.stat(clone / "a.txt")
        again = self.fixture_git("commit-tree", "HEAD^{tree}", "-p", "HEAD", "-m", "a commit")  # the same tree
        self.linux["sync"](self.git, clone, str(self.source), again)
        after = os.stat(clone / "a.txt")
        self.assertEqual((after.st_mtime_ns, after.st_ino), (before.st_mtime_ns, before.st_ino))
        self.assertEqual((clone / "build" / "linux-gcc" / "object.o").read_bytes(), b"built")
        self.assertFalse((clone / "stray.txt").exists())
        changed = {"a.txt": b"two\r\n", "new.txt": b"new\n", "tests/fixtures/entries.json": b"[1]\r\n"}
        for rel, data in {".gitattributes": (ROOT / ".gitattributes").read_bytes(), **changed}.items():
            (self.source / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.source / rel).write_bytes(data)
        index = (self.source / ".git" / "index").read_bytes()
        self.linux["sync"](self.git, clone, str(self.source), self.linux["snapshot"](self.source))
        self.assertEqual({rel: (clone / rel).read_bytes() for rel in changed}, {**changed, "a.txt": b"two\n"})
        self.assertEqual((self.source / ".git" / "index").read_bytes(), index)
        self.assertEqual(self.linux["earlier_clones"](self.base), ["linux-gcc"])
        self.assertEqual((self.old / "kept.txt").read_bytes(), b"old")

    def inside(self, way, drive):
        """inside() with the sync and the drive faked: [(sync's clone)], the
        report's lines."""
        synced = []
        saved = {k: self.linux[k] for k in ("sync", "drive")}
        self.linux["sync"] = lambda git, clone, source, commit: synced.append(clone)
        self.linux["drive"] = drive
        jobs = [n for n, job in LOOP.JOBS.items() if job[0] == "linux"]
        args = argparse.Namespace(way=way, jobs=jobs, fuzz_time=None, pins=None, prefix="/nonexistent/prefix",
                                  clone=str(self.base), source=str(self.source), commit=self.first)
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                counts = self.linux["inside"](args, LOOP.Report(LOOP.LINES))
        finally:
            self.linux.update(saved)
        self.assertEqual(counts[1:], [0, 0], out.getvalue())
        return synced, out.getvalue()

    def test_once_a_run(self):
        """One sync a run, before every unit; each unit driven in the one
        clone under its own TMPDIR, a job's trees its build/JOB."""
        driven = []

        def drive(report, units, root, logs, command, env, phase="test", pins=None):
            driven.append((units[0], root, env["TMPDIR"]))
            return [1, 0, 0]

        for way in ("2", "3"):
            driven.clear()
            synced, out = self.inside(way, drive)
            clone = self.base / self.linux["CLONE"]
            self.assertEqual(synced, [clone], out)
            names = [u.name for u, _, _ in driven]
            self.assertEqual(names, [u.name for u in LOOP.plan(way, "linux", [], None)])
            self.assertEqual({root for _, root, _ in driven}, {clone})
            self.assertEqual(len({temp for _, _, temp in driven}), len(driven))
            self.assertFalse(any(Path(temp).exists() for _, _, temp in driven))
            for unit, _, _ in driven:
                self.assertEqual(unit.trees, [] if unit.name in LOOP.TABLE["READS"] else ["build/" + unit.name])
            self.assertIn(f"loop: logs in {clone / 'build' / 'loop'}", out)

    def test_tier3_empties_build_trees(self):
        """Tier 2 then tier 3 on fakes in a synced clone: each job's tree
        kept, then emptied with --fresh; the clone's files, its git
        directory and the earlier clones untouched; the suites read the
        jobs' trees and own none."""
        clone = self.base / self.linux["CLONE"]
        shutil.copytree(self.source, clone)  # a synced clone, its git directory too (the sync: test_sync)
        presets = LOOP.binary_dirs()
        code = "import json, sys; open(sys.argv[1], 'a').write(json.dumps(sys.argv[2:]) + '\\n')"

        def command(unit, step):
            return [sys.executable, "-I", "-c", code, str(clone / "build" / f"ran-{unit.name}.txt"),
                    *LOOP.local_argv(unit, step, presets)]

        jobs = [n for n, job in LOOP.JOBS.items() if job[0] == "linux"]
        for way in ("2", "3"):
            if way == "3":
                for name in jobs:
                    (clone / "build" / name / "object.o").write_bytes(b"built")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                counts = LOOP.drive(LOOP.Report(LOOP.LINES), LOOP.plan(way, "linux", jobs, None), clone,
                                    clone / "build" / "loop", command, dict(os.environ))
            self.assertEqual(counts[1:], [0, 0], out.getvalue())
        text = out.getvalue()
        for name in jobs:
            self.assertIn(f"{name} tree: {clone / 'build' / name} emptied, a fresh run", text)
            self.assertFalse((clone / "build" / name / "object.o").exists())
        self.assertNotIn("linux-full tree", text)
        self.assertEqual((clone / "a.txt").read_bytes(), b"one\n")
        self.assertTrue((clone / ".git").is_dir())
        self.assertEqual((self.old / "kept.txt").read_bytes(), b"old")
        ran = {name: [json.loads(x) for x in (clone / "build" / f"ran-{name}.txt").read_text().splitlines()]
               for name in ("linux-gcc", "linux-full")}
        self.assertIn("--fresh", ran["linux-gcc"][-len(LOOP.JOBS["linux-gcc"][2])])
        self.assertEqual(ran["linux-full"], [["ctest", "--preset", "full", "--test-dir", "build/linux-plain"]])
