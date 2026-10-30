"""The loop's tests start git by one door, which counts each start
(tools/loop_git.py), so that the count is every start: no file of the
loop's tools or of any test names git's program another way, in any case,
by a path, as a plain, prefixed or f-string literal; the product's tests,
which never import the door, start none. A program held in a variable no
reading of the source sees: the counting shim, before every Python test
of the loop's build, counts that start (tools/loop_shim.py). The door counts a real git's start and
not a fake's; a test step of the loop whose tests start more than the
limit fails, its line naming the count, one at the limit passing with it
named; and a run whose steps start more than the run's limit in all fails,
its last line naming the total, one at the limit passing with it named."""

import ast
import contextlib
import io
import os
import re
import shutil
import sys
import tempfile
import tokenize
import unittest
from pathlib import Path

from tests.drive.test_loop_ci import ROOT, load_loop
from tools import loop_git

LOOP = load_loop()
# The door, this file's counts, which start nothing, B2's rule naming git's literals, and the shim's
# runner, which names the shim's file by git's program and finds the real git before the shim.
OWN = ("tools/loop_git.py", "tests/drive/test_git_count.py", "tests/structure/rules.py", "tools/loop_shim.py")
# git's program in any case, alone, by an absolute path, or as git.exe by any
# path; a relative directory (".config/git") is none. A shell's command line
# ("git status"), as messages read, is left to the shim.
PROGRAM = re.compile(r"(?:(?:[A-Za-z]:)?[/\\](?:.*[/\\])?)?git(?:\.exe)?|.*[/\\]git\.exe", re.IGNORECASE | re.DOTALL)
# The tokens that open and close an f-string (and a template string), where the tokenizer has them.
OPENS = {getattr(tokenize, n) for n in ("FSTRING_START", "TSTRING_START") if hasattr(tokenize, n)}
CLOSES = {getattr(tokenize, n) for n in ("FSTRING_END", "TSTRING_END") if hasattr(tokenize, n)}
MIDDLES = {getattr(tokenize, n) for n in ("FSTRING_MIDDLE", "TSTRING_MIDDLE") if hasattr(tokenize, n)}


def literals(path):
    """The string literals of a Python file, by line: a plain or prefixed
    string's value, and an f-string's literal parts joined, its fields left
    out (each f-string within a field its own)."""
    open_parts = []  # the literal parts of each f-string still open, by its line
    with open(path, "rb") as f:
        for token in tokenize.tokenize(f.readline):
            if token.type == tokenize.STRING:
                value = ast.literal_eval(token.string)
                yield token.start[0], value.decode("latin-1") if isinstance(value, bytes) else value
            elif token.type in OPENS:
                open_parts.append((token.start[0], []))
            elif token.type in MIDDLES and open_parts:
                open_parts[-1][1].append(token.string)
            elif token.type in CLOSES and open_parts:
                line, parts = open_parts.pop()
                yield line, "".join(parts)


def named(path):
    """The lines of a Python file whose literal names git's program."""
    return [n for n, text in literals(path) if PROGRAM.fullmatch(text)]


class Door(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_one_door(self):
        """No literal names git's program outside the door: every start
        of git in tools/ and tests/ by a name in the source is the door's,
        so counted."""
        found = []
        for top in ("tools", "tests"):
            for path in sorted((ROOT / top).rglob("*.py")):
                rel = path.relative_to(ROOT).as_posix()
                if rel not in OWN and "__pycache__" not in rel:
                    found += [f"{rel}:{n}" for n in named(path)]
        self.assertEqual(found, [])

    def test_guard_planted(self):
        """Each form that names git's program, planted in a scratch file,
        is caught; text that names none is not."""
        caught = ['run(["Git", "status"])', 'run(["GIT.EXE"])', 'run([f"git", *argv])', 'run([f"{top}/Git"])',
                  'run([r"git"])', 'run([b"git"])', 'run(["/usr/bin/git"])', 'run(["X:\\\\tools\\\\git.exe"])',
                  'run(F"GIT", x)', 'run([f"{f\'{x}\'}git"])']
        clean = ['p = ".git"', 'k = "GIT_DIR"', 'm = "git\'s door"', 'a = ".gitattributes"', 'n = f"{git}"',
                 'w = f"digit {x}"', 'd = "a git."', 's = f"git {x}: failed"', 'c = ".config/git"']
        top = Path(tempfile.mkdtemp(prefix="qiven3-git-guard-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        plant = top / "plant.py"
        plant.write_text("\n".join(caught + clean) + "\n", encoding="utf-8")
        self.assertEqual(named(plant), list(range(1, len(caught) + 1)))

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

    def steps(self, *starts, inherited=None):
        """A loop's ctest steps, each of whose test starts git as many times
        as starts gives, as the door counts them, then the run's last line;
        (counts, the steps' lines, the last line, the exit status). With
        inherited, the run's environment names that count already."""
        logs = Path(tempfile.mkdtemp(prefix="qiven3-git-count-"))
        self.addCleanup(shutil.rmtree, logs, ignore_errors=True)
        steps = [LOOP.Step(f"fake{i}", ("ctest", "--preset", "fake"), after=None) for i in range(len(starts))]
        by_name = {step.name: n for step, n in zip(steps, starts)}

        def command(step):
            return [sys.executable, "-I", "-c", f"import sys; sys.path.insert(0, {str(ROOT)!r}); "
                    f"from tools import loop_git; [loop_git.counted('git', None) for _ in range({by_name[step.name]})]"]

        env = {k: v for k, v in os.environ.items() if k != loop_git.COUNT}  # each step's count its own
        if inherited is not None:
            env[loop_git.COUNT] = str(inherited)
        report = LOOP.Report(LOOP.LINES)
        report.plan(len(steps))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            counts = LOOP.run_steps(report, "job", steps, "test", command, logs, logs, env)
            code = LOOP.RUN["ending"](report, counts, LOOP.RUN["now"]())
        lines = out.getvalue().splitlines()
        return counts, [line for line in lines if line.startswith("job fake")], lines[-1], code

    def test_step_limit(self):
        """A step at the limit passes, its count named; one past it fails,
        named."""
        counts, lines, _, _ = self.steps(loop_git.LIMIT)
        self.assertEqual(counts, [1, 0, 0], lines)
        self.assertRegex(lines[0], rf"^job fake0: pass in \S+ s; git {loop_git.LIMIT}$")
        counts, lines, _, _ = self.steps(loop_git.LIMIT + 1)
        self.assertEqual(counts, [0, 1, 0], lines)
        self.assertRegex(lines[0], rf"^job fake0: FAIL, {loop_git.LIMIT + 1} git processes started, over "
                                   rf"{loop_git.LIMIT}, in \S+ s; git {loop_git.LIMIT + 1}; log ")

    def test_run_limit(self):
        """A run whose steps, each within its own limit, start the run's
        limit in all passes, the total on its last line; one more fails,
        exit 1, named; a run's total is its own steps'."""
        half = loop_git.RUN_LIMIT // 2
        self.assertLessEqual(half, loop_git.LIMIT)
        counts, lines, last, code = self.steps(half, loop_git.RUN_LIMIT - half)
        self.assertEqual((counts, code), ([2, 0, 0], 0), lines)
        self.assertRegex(last, rf"^loop: 2 passed, 0 failed, 0 skipped in \S+ s; git {loop_git.RUN_LIMIT}; exit 0$")
        counts, lines, last, code = self.steps(half, loop_git.RUN_LIMIT - half, 1)
        self.assertEqual((counts, code), ([3, 0, 0], 1), lines)
        self.assertRegex(last, rf"^loop: 3 passed, 0 failed, 0 skipped in \S+ s; git {loop_git.RUN_LIMIT + 1}, "
                               rf"{loop_git.RUN_LIMIT + 1} git processes started in the run, over "
                               rf"{loop_git.RUN_LIMIT}; exit 1$")

    def test_inherited(self):
        """A count the environment names already is replaced for each step,
        never trusted: both bounds hold and the totals are the real starts,
        each step's added to that count as well (an enclosing run's)."""
        top = Path(tempfile.mkdtemp(prefix="qiven3-git-count-"))
        self.addCleanup(shutil.rmtree, top, ignore_errors=True)
        over = loop_git.LIMIT + 10
        counts, lines, last, code = self.steps(over, over, inherited=top / "outer")
        self.assertEqual((counts, code), ([0, 2, 0], 1), lines)
        self.assertTrue(all(f"; git {over}; log " in line for line in lines[:2]), lines)
        self.assertRegex(last, rf"; git {2 * over}, {2 * over} git processes started in the run, over "
                               rf"{loop_git.RUN_LIMIT}; exit 1$")
        self.assertEqual(loop_git.verdict(top / "outer")[0], 2 * over)


if __name__ == "__main__":
    unittest.main()
