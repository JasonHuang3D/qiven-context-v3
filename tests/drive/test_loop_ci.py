"""The loop's jobs against CI's: tools/loop.py's JOBS and ENV, read against
.github/workflows/ci.yml and msan.yml, so that the loop and CI cannot
drift. Each job of the workflows but macOS's is the loop's, by the same
preset; each step of a job that runs a preset's build or reads its tree is
the loop's step, in CI's order, with CI's command, the loop's own additions
apart; and the sanitizers' options and CTest's parallel level are CI's."""

import re
import shlex
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
CI_ONLY = {"macos-clang"}  # no machine for it (the loop's text)


def load_loop():
    from tools import loop
    return loop


def blocks(lines, indent):
    """The lines under each key at this indent: {key: [lines]}."""
    out, key = {}, None
    for line in lines:
        m = re.match(r"^( *)([A-Za-z0-9_-]+):(.*)$", line)
        if m and len(m.group(1)) == indent:
            key = m.group(2)
            out[key] = [line]
        elif key is not None and (not line.strip() or len(line) - len(line.lstrip()) > indent):
            out[key].append(line)
        elif line.strip() and not line.lstrip().startswith("#"):
            key = None
    return out


def env_of(lines, indent):
    """A mapping block's scalar values, comments and block scalars left out."""
    out = {}
    for line in lines[1:]:
        m = re.match(r"^ {%d}([A-Z0-9_]+): (.*)$" % indent, line)
        if m and m.group(2).strip() != "|":
            out[m.group(1)] = m.group(2).strip().strip('"')
    return out


def steps_of(job_lines):
    """Each step: (if, [run lines])."""
    steps, current = [], None
    for line in job_lines:
        if re.match(r"^      - ", line):
            current = {"if": "", "run": [], "in_run": False}
            steps.append(current)
            line = "        " + line[8:]
        if current is None:
            continue
        m = re.match(r"^        (if|run): ?(.*)$", line)
        if m:
            current["in_run"] = m.group(1) == "run"
            if m.group(1) == "if":
                current["if"] = m.group(2)
            elif m.group(2).strip() not in ("|", ""):
                current["run"].append(m.group(2).strip())
        elif re.match(r"^        [a-z-]+:", line):
            current["in_run"] = False
        elif current["in_run"] and line.startswith("          "):
            current["run"].append(line.strip())
    return steps


def holds(condition, matrix):
    """A step's if: every matrix term of the conjunction holds."""
    for name, value in re.findall(r"matrix\.(\w+)(?:\s*==\s*'([^']*)')?", condition):
        got = matrix.get(name)
        if (value and got != value) or (not value and not got):
            return False
    return True


def commands(step, matrix):
    """A step's commands as the loop compares them; none if the step runs
    no preset's build and reads no build tree."""
    text = "\n".join(step["run"])
    if not re.search(r"--preset|build/ci|clean_check\.py", text):
        return []
    out = []
    for line in step["run"]:
        line = line.replace("${{ matrix.preset }}", matrix["preset"])
        if "QIVEN3_PYTHON" in line and " -I " in line and "clean_check.py" in text:
            words = shlex.split(line)
            out.append(("clean-check", words[words.index("-I") + 2]))
            continue
        if not re.match(r"(cmake|ctest) ", line):
            continue
        line = re.split(r" \|\| | 2>&1| \| ", line)[0]
        words = [w for w in shlex.split(line) if not w.startswith("@")]
        if words and words[0] in ("cmake", "ctest") and "--version" not in words:
            out.append(tuple(words))
    return out


def ci_jobs():
    """{job: (preset, [commands], env)} of both workflows."""
    jobs = {}
    for name in ("ci.yml", "msan.yml"):
        lines = (WORKFLOWS / name).read_text(encoding="utf-8").splitlines()
        top = blocks(lines, 0)
        top_env = env_of(top.get("env", []), 2)
        for job_name, job_lines in blocks(top["jobs"][1:], 2).items():
            job_env = dict(top_env, **env_of(blocks(job_lines[1:], 4).get("env", []), 6))
            text = "\n".join(job_lines)
            if "include:" in text:
                matrices, current = [], None
                for line in job_lines:
                    m = re.match(r"^ +(- )?(job|preset|clean|analysis): (.*)$", line)
                    if m and m.group(1):
                        current = {}
                        matrices.append(current)
                    if m and current is not None:
                        current[m.group(2)] = m.group(3).strip()
            elif re.search(r"^        preset: \[", text, re.MULTILINE):
                listed = re.search(r"^        preset: \[(.*)\]", text, re.MULTILINE).group(1)
                matrices = [{"job": p.strip(), "preset": p.strip()} for p in listed.split(",")]
            else:
                title = re.search(r"^    name: (\S+)", text, re.MULTILINE).group(1)
                presets = set(re.findall(r"--preset (\S+)", text)) - {"fake"}
                matrices = [{"job": title, "preset": presets.pop() if len(presets) == 1 else title}]
            for matrix in matrices:
                cmds = [c for s in steps_of(job_lines) if holds(s["if"], matrix) for c in commands(s, matrix)]
                jobs[matrix["job"]] = (matrix["preset"], cmds, job_env)
    return jobs


class LoopAgainstCI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.loop = load_loop()
        cls.ci = ci_jobs()

    def test_jobs(self):
        self.assertEqual(set(self.ci) - CI_ONLY, set(self.loop.JOBS))
        for name, (system, preset, _) in self.loop.JOBS.items():
            with self.subTest(job=name):
                self.assertEqual(preset, self.ci[name][0])
                self.assertEqual(system, name.split("-")[0])

    def test_steps(self):
        for name, (_, _, steps) in self.loop.JOBS.items():
            with self.subTest(job=name):
                self.assertEqual([s.ci for s in steps], self.ci[name][1])

    def test_additions(self):
        """The loop's own additions: a fresh cache on each configure, and the
        pins on Windows', as the text states; nothing else."""
        for name, (system, _, steps) in self.loop.JOBS.items():
            for step in steps:
                with self.subTest(job=name, step=step.name):
                    if step.ci[:2] == ("cmake", "--preset"):
                        want = {"windows": {"-DQIVEN3_PIN_TOOLS=ON"}, "linux": set()}[system]
                        self.assertEqual(set(step.local) - {"--fresh"}, want)
                    else:
                        self.assertEqual(step.local, ())

    def test_environment(self):
        for name, (system, _, _) in self.loop.JOBS.items():
            ci_env = self.ci[name][2]
            for key, value in self.loop.ENV[system].items():
                if key in ci_env:
                    with self.subTest(job=name, key=key):
                        self.assertEqual(value, ci_env[key])
            for key in ci_env:
                if key.endswith("_OPTIONS") or key == "CTEST_PARALLEL_LEVEL":
                    with self.subTest(job=name, key=key):
                        self.assertIn(key, self.loop.ENV[system])

    def test_parser_sees_each_kind(self):
        """The parser's controls: each workflow's shapes found, so that an
        empty reading cannot pass."""
        self.assertIn("windows-analysis", self.ci)
        self.assertIn(("ctest", "--preset", "ci-analysis", "--no-tests=error"), self.ci["windows-analysis"][1])
        self.assertIn(("clean-check", "build/ci"), self.ci["linux-plain"][1])
        self.assertIn(("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=error", "-L", "^cxx$"),
                      self.ci["linux-msan"][1])
        self.assertEqual(self.ci["linux-gcc"][2]["TSAN_OPTIONS"], "halt_on_error=1:second_deadlock_stack=1")
        self.assertEqual(self.ci["linux-msan"][2]["MSAN_OPTIONS"], "halt_on_error=1")
        self.assertTrue(all(len(c) >= 3 for c in self.ci.values()))
        self.assertTrue(all(len(c[1]) >= 3 for c in self.ci.values()))


if __name__ == "__main__":
    unittest.main()
