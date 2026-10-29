"""The loop's tests start git by one door, which counts each start
(tools/loop_git.py; ruling 43.3 item 3): no file of the loop's tools or of
any test starts git another way, so that the count is every start and
the product's tests, which never import the door, start none; the door
counts a real git's start and not a fake's; and a test step of the loop
whose tests start more than the limit fails, its line naming the count,
one at the limit passing with it named."""

import contextlib
import io
import os
import shutil
import sys
import tempfile
import tokenize
import unittest
from pathlib import Path

from tests.drive.test_loop_ci import ROOT, load_loop
from tools import loop_git

LOOP = load_loop()
# The door, this file's counts, which start nothing, and B2's rule naming git's literals.
OWN = ("tools/loop_git.py", "tests/drive/test_git_count.py", "tests/structure/rules.py")


def literals(path):
    """The plain string literals of a Python file, by line."""
    with open(path, "rb") as f:
        for token in tokenize.tokenize(f.readline):
            if token.type == tokenize.STRING and token.string[:1] in "'\"":
                yield token.start[0], token.string.strip("'\"")


class Door(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_one_door(self):
        """No literal names git's program outside the door: every start
        of git in tools/ and tests/ is the door's, so counted."""
        found = []
        for top in ("tools", "tests"):
            for path in sorted((ROOT / top).rglob("*.py")):
                rel = path.relative_to(ROOT).as_posix()
                if rel not in OWN and "__pycache__" not in rel:
                    found += [f"{rel}:{n}" for n, text in literals(path) if text in ("git", "git.exe")]
        self.assertEqual(found, [])

    def test_counted(self):
        """One file a real git's start, none for a fake's; the verdict
        faults past the limit alone."""
        top = Path(tempfile.mkdtemp(prefix="qiven3-git-count-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        env = {loop_git.COUNT: str(top / "count")}
        loop_git.counted(str(ROOT / "tests" / "drive" / "fake_git.py"), env)
        self.assertEqual(loop_git.verdict(top / "count"), (0, None))
        for _ in range(loop_git.LIMIT):
            loop_git.counted("git", env)
        self.assertEqual(loop_git.verdict(top / "count"), (loop_git.LIMIT, None))
        loop_git.counted("git", env)
        self.assertEqual(loop_git.verdict(top / "count"),
                         (loop_git.LIMIT + 1, f"{loop_git.LIMIT + 1} git processes started, over {loop_git.LIMIT}"))

    def step(self, starts):
        """A ctest step of a loop whose test starts git starts times, as
        the door counts them; (counts, its line)."""
        logs = Path(tempfile.mkdtemp(prefix="qiven3-git-count-"))
        self.addCleanup(shutil.rmtree, logs, ignore_errors=True)
        steps = [LOOP.Step("fake", ("ctest", "--preset", "fake"), after=None)]
        code = (f"import sys; sys.path.insert(0, {str(ROOT)!r}); from tools import loop_git; "
                f"[loop_git.counted('git', None) for _ in range({starts})]")
        env = {k: v for k, v in os.environ.items() if k != loop_git.COUNT}  # the step's count its own
        report = LOOP.Report(LOOP.LINES)
        report.plan(1)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            counts = LOOP.run_steps(report, "job", steps, "test", lambda step: [sys.executable, "-I", "-c", code],
                                    logs, logs, env)
        return counts, [line for line in out.getvalue().splitlines() if line.startswith("job fake:")]

    def test_step_limit(self):
        """A step at the limit passes, its count named; one past it fails,
        named."""
        counts, lines = self.step(loop_git.LIMIT)
        self.assertEqual(counts, [1, 0, 0], lines)
        self.assertRegex(lines[0], rf"^job fake: pass in \S+ s; git {loop_git.LIMIT}$")
        counts, lines = self.step(loop_git.LIMIT + 1)
        self.assertEqual(counts, [0, 1, 0], lines)
        self.assertRegex(lines[0], rf"^job fake: FAIL, {loop_git.LIMIT + 1} git processes started, over "
                                   rf"{loop_git.LIMIT}, in \S+ s; git {loop_git.LIMIT + 1}; log ")


if __name__ == "__main__":
    unittest.main()
