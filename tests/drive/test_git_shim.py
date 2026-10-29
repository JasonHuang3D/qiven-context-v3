"""The door's count checked by a counting shim: the loop's tests that start
git (test_loop, test_loop_git and test_line_endings) and those that run
on the fake (test_loop_pins, test_loop_select, test_nav), each group run
again in a child process with a shim placed first on PATH under git's
program name, which logs each start and runs the real git
(tools/git_shim.c, built here by the toolset's cl, on Windows;
tools/git_shim.sh on Linux). A group fails when its tests fail, or when
the shim's starts differ from the door's (tools/loop_git.py) for the same
run: a start past the door, by a name the guard reads
(tests/drive/test_git_count.py) or a program held in a variable, which no
reading of the source sees.

The child's starts are counted in a directory of the group's own, the
shim's log and the door's count beside it, not in the enclosing step's: a
step's count is its own tests' starts. The real git is the door's, found
before the shim is placed; the shim's log is a file this test makes,
never under HOME, which test_loop_git changes. Each group stays under
about 15 seconds on a CI Windows job: test_loop runs by its classes,
test_nav by its cases' cost, a case of clangd's index alone or with a
short one."""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive.test_loop_ci import ROOT
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
}
RUN = ("import sys, unittest; sys.path.insert(0, sys.argv[1]); "
       "unittest.main(module=None, argv=['unittest', *sys.argv[2:]])")
SHIM_LOG, SHIM_REAL = "QIVEN3_SHIM_LOG", "QIVEN3_SHIM_REAL"


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


class Groups(unittest.TestCase):
    """The shim, built once a class, and a group's run under it; no test of
    its own."""
    ctest_labels = ("fake",)
    ctest_reads = ("tools/git_shim.c", "tools/git_shim.sh")

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
        starts are the door's."""
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
        text = done.stderr.decode("utf-8", "replace")
        self.assertEqual(done.returncode, 0, text[-3000:])
        starts = log.read_text(encoding="utf-8", errors="replace").splitlines()
        door = loop_git.verdict(count)[0]
        self.assertEqual(len(starts), door, "the shim's starts against the door's:\n" + "\n".join(starts[-60:]))
        return door, [line for line in text.splitlines() if line.startswith(("Ran ", "OK", "FAILED"))]


def ids(names):
    """The test ids unittest loads from names, by a loader of their own (the
    default one holds the running test's name patterns)."""
    def walk(suite):
        for test in suite:
            yield from walk(test) if isinstance(test, unittest.TestSuite) else (test.id(),)

    return set(walk(unittest.TestLoader().loadTestsFromNames(names)))


class Shim(Groups):
    def test_groups_cover(self):
        """The groups hold every test of their modules, each once."""
        named = [n for names in GROUPS.values() for n in names]
        modules = sorted({".".join(n.split(".")[:3]) for n in named})
        per_group = [ids(names) for names in GROUPS.values()]
        self.assertEqual(sum(len(g) for g in per_group), len(set().union(*per_group)))
        self.assertEqual(set().union(*per_group), ids(modules))

    def test_loop_git(self):
        self.group("loop_git")

    def test_fake(self):
        self.group("fake")

    def test_nav_lookups(self):
        self.group("nav_lookups")

    def test_nav_truncated(self):
        self.group("nav_truncated")

    def test_nav_types(self):
        self.group("nav_types")

    def test_nav_index_used(self):
        self.group("nav_index_used")

    def test_nav_index_stale_file(self):
        self.group("nav_index_stale_file")

    def test_nav_index_stale_clangd(self):
        self.group("nav_index_stale_clangd")

    def test_loop(self):
        self.group("loop")

    def test_line_endings(self):
        self.group("line_endings")


class ShimAlone(Groups):
    """The groups of test_loop's classes that run alone, alone as they."""
    ctest_serial = True

    def test_loop_windows(self):
        self.group("loop_windows")

    def test_loop_linux(self):
        self.group("loop_linux")


if __name__ == "__main__":
    unittest.main()
