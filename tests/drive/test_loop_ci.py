"""The loop's jobs against CI's: tools/loop_jobs.py's JOBS, ENV and
LINUX_SHELL, read against .github/workflows/ci.yml and msan.yml, so that the
loop and CI cannot drift. Each job of the workflows but macOS's is the
loop's, by the same preset; each step of a job that runs a preset's build or
reads its tree is the loop's step, in CI's order, with CI's command; the
sanitizers' options, CTest's parallel level and the Linux shell are CI's.
The pins are the loop's alone, as its additions to CI's configure: the
pinned tools' option (tier 3 adds a fresh cache, tests/drive/test_loop.py), and on Linux the pinned compilers of the preset's
family, each a pin of tools/linux-pins.txt at the root CMakeLists.txt's
version. The loop's own jobs, LOCAL, are in no workflow: windows-symbols
builds release, which keeps its PDBs, and runs fake's selection and then
the debugger preset, a label no workflow runs; nor are its own steps,
LOCAL_STEPS, after CI's: linux-plain's debugger step, that label on its
build.

The tiers' own steps are read against CI's and the presets: tier 1's
structure steps are windows-analysis's; its fast step is the fast test
preset's selection on the job's own build; tier 3's suites build the
structure, nohal and fuzz presets and run the full and fuzz test presets.
Each Windows job's trees are its own: no two jobs share one.

Neither workflow runs a draft pull request's jobs: each job's if is DRAFT,
and the pull request's event types are READY, ready_for_review among them,
so that marking one ready runs them; a workflow copy without either is
caught."""

import json
import re
import shlex
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
CI_ONLY = {"macos-clang"}  # no machine for it (the loop's text)
PIN = "-DQIVEN3_PIN_TOOLS=ON"
FAMILY = {"g++": "GCC", "clang++": "CLANG"}  # a CI preset's compiler: its pin's name in CMakeLists.txt
DRAFT = "${{ !github.event.pull_request.draft }}"  # each job's if: a draft pull request runs none
READY = {"opened", "synchronize", "reopened", "ready_for_review"}  # the pull request's event types


def preset_compilers():
    """{configure preset: (C compiler, C++ compiler)} of CMakePresets.json."""
    presets = json.loads((ROOT / "CMakePresets.json").read_bytes())["configurePresets"]
    return {p["name"]: (p.get("cacheVariables", {}).get("CMAKE_C_COMPILER"),
                        p.get("cacheVariables", {}).get("CMAKE_CXX_COMPILER")) for p in presets}


def linux_pins():
    """{command: version} of tools/linux-pins.txt's command lines."""
    out = {}
    for line in (ROOT / "tools" / "linux-pins.txt").read_text(encoding="utf-8").splitlines():
        words = line.split("#", 1)[0].split()
        if words[:1] == ["command"] and len(words) >= 3:
            out[words[1]] = words[2]
    return out


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


def draft_findings(text):
    """Each way a workflow runs a draft pull request's jobs, or runs none
    when one is marked ready."""
    top = blocks(text.splitlines(), 0)
    found = []
    events = blocks(top.get("on", [""])[1:], 2)
    request = blocks(events.get("pull_request", [""])[1:], 4)
    types = request.get("types", ["types:"])[0].split(":", 1)[1].strip()
    if not (types.startswith("[") and types.endswith("]")) or \
            {t.strip() for t in types[1:-1].split(",")} != READY:
        found.append(f"pull request types {types or None}")
    jobs = blocks(top.get("jobs", [""])[1:], 2)
    if not jobs:
        found.append("no jobs")
    for job, job_lines in jobs.items():
        conditions = [line.split(":", 1)[1].strip() for line in blocks(job_lines[1:], 4).get("if", [])[:1]]
        if conditions != [DRAFT]:
            found.append(f"job {job} runs a draft pull request")
    return found


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

    def ci_jobs(self):
        """The loop's jobs of CI's, the loop's own steps dropped."""
        return {k: (system, preset, [s for s in steps if s not in self.loop.LOCAL_STEPS.get(k, ())])
                for k, (system, preset, steps) in self.loop.JOBS.items() if k not in self.loop.LOCAL}

    def test_jobs(self):
        self.assertEqual(set(self.ci) - CI_ONLY, set(self.ci_jobs()))
        self.assertFalse(set(self.ci) & set(self.loop.LOCAL))
        for name, (system, preset, _) in self.ci_jobs().items():
            with self.subTest(job=name):
                self.assertEqual(preset, self.ci[name][0])
                self.assertEqual(system, name.split("-")[0])

    def test_steps(self):
        for name, (_, _, steps) in self.ci_jobs().items():
            with self.subTest(job=name):
                self.assertEqual([s.ci for s in steps], self.ci[name][1])

    def test_additions(self):
        """The loop's own additions: the pinned tools' option on each
        configure, and on Linux's the pinned compilers in place
        of the preset's, as the text states; nothing else."""
        compilers = preset_compilers()
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        pins = linux_pins()
        for name, (system, preset, steps) in self.ci_jobs().items():
            for step in steps:
                with self.subTest(job=name, step=step.name):
                    if step.ci[:2] == ("cmake", "--preset") and step.name == "configure":
                        self.assertEqual(step.local[:1], (PIN,))
                        rest = step.local[1:]
                        if system == "windows":
                            self.assertEqual(rest, ())
                            continue
                        c, cxx = compilers[preset]
                        self.assertEqual(len(rest), 2, rest)
                        got_c, got_cxx = (x.split("=", 1)[1] for x in rest)
                        self.assertEqual(rest[0].split("=")[0] + rest[1].split("=")[0],
                                         "-DCMAKE_C_COMPILER-DCMAKE_CXX_COMPILER")
                        version = re.search(r"^set\(QIVEN3_PIN_%s (\S+)\)$" % FAMILY[cxx], cmake, re.MULTILINE)
                        self.assertIsNotNone(version, cxx)
                        for unpinned, pinned in ((c, got_c), (cxx, got_cxx)):
                            self.assertEqual(pinned, f"{unpinned}-{version.group(1).split('.')[0]}")
                            self.assertEqual(pins.get(pinned), version.group(1))
                    elif step.ci[:2] == ("cmake", "--preset"):
                        self.assertEqual(step.local, (PIN,))
                    else:
                        self.assertEqual(step.local, ())

    def test_shell(self):
        """CI's Linux steps run under the loop's LINUX_SHELL, then bash."""
        want = " ".join(self.loop.LINUX_SHELL) + " bash "
        for name in ("ci.yml", "msan.yml"):
            text = (WORKFLOWS / name).read_text(encoding="utf-8")
            shells = re.findall(r"^ +shell: (.*)$", text, re.MULTILINE)
            linux = [s for s in shells if s not in ("pwsh", "bash")]
            with self.subTest(workflow=name):
                self.assertTrue(linux, shells)
                self.assertTrue(all(s.startswith(want) for s in linux), shells)

    def test_local(self):
        """windows-symbols: release, whose PDBs are kept, in its own tree;
        fake's selection there, then the debugger preset on release."""
        presets = json.loads((ROOT / "CMakePresets.json").read_bytes())
        configure = {p["name"]: p for p in presets["configurePresets"]}
        tests = {p["name"]: p for p in presets["testPresets"]}
        self.assertEqual(set(self.loop.LOCAL), {"windows-symbols"})
        system, preset, steps = self.loop.JOBS["windows-symbols"]
        self.assertEqual((system, preset), ("windows", "release"))
        self.assertEqual(configure["release"]["cacheVariables"]["CMAKE_BUILD_TYPE"], "RelWithDebInfo")
        self.assertEqual(self.loop.TREES["windows-symbols"], {"build/release": "build/release"})
        self.assertEqual([s.name for s in steps], ["configure", "build", "fake", "debugger"])
        self.assertEqual(steps[0].local, (PIN,))
        fake = tests["fake"]["filter"]
        self.assertEqual(steps[2].ci, ("ctest", "--test-dir", "build/release", "--output-on-failure",
                                       "--no-tests=error", "-L", fake["include"]["label"], "-LE",
                                       fake["exclude"]["label"]))
        self.assertEqual(steps[3].ci, ("ctest", "--preset", "debugger"))
        self.assertEqual(tests["debugger"]["configurePreset"], "release")
        self.assertEqual(tests["debugger"]["filter"]["include"]["label"], "^debugger$")

    def test_local_steps(self):
        """linux-plain's debugger step, after CI's: the debugger label on
        its build, a preset with debug information; no other."""
        presets = {p["name"]: p for p in json.loads((ROOT / "CMakePresets.json").read_bytes())["configurePresets"]}
        self.assertEqual(set(self.loop.LOCAL_STEPS), {"linux-plain"})
        system, preset, steps = self.loop.JOBS["linux-plain"]
        self.assertEqual((system, preset), ("linux", "linux-plain"))
        self.assertEqual(presets["linux-plain"]["cacheVariables"]["CMAKE_BUILD_TYPE"], "RelWithDebInfo")
        local = list(self.loop.LOCAL_STEPS["linux-plain"])
        self.assertEqual(steps[-len(local):], local)
        self.assertEqual([(s.name, s.ci, s.local) for s in local],
                         [("debugger", ("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=error",
                                        "-L", "^debugger$"), ())])
        self.assertNotIn("debugger", [s.name for s in self.ci_jobs()["linux-plain"][2]])

    def test_tiers(self):
        """The tiers' own steps against CI's and the presets: tier 1's
        structure steps are windows-analysis's, as ci.yml writes them; its
        fast step is the fast test preset's selection, on the job's build;
        tier 3's suites build the structure, nohal and fuzz presets and run
        the full and fuzz test presets on Windows, and run them on Linux on
        linux-gcc's and linux-fuzz's trees, read and never owned. No two
        jobs of a system share a tree, and none is CI's build/ci."""
        presets = json.loads((ROOT / "CMakePresets.json").read_bytes())
        tests = {p["name"]: p for p in presets["testPresets"]}
        table = self.loop.TABLE
        self.assertEqual(table["TIER1"], {"windows": "windows-msvc", "linux": "linux-gcc"})
        msvc = list(table["TIER1_STEPS"]["windows-msvc"])
        analysis = self.ci["windows-analysis"][1]
        self.assertEqual([s.ci for s in msvc[:3]], analysis[-3:])
        self.assertEqual([s.name for s in msvc[:3]], ["structure configure", "structure build", "structure tests"])
        fast = table["FAST"]
        self.assertEqual(msvc[3:], [fast])
        self.assertEqual(list(table["TIER1_STEPS"]["linux-gcc"]), [fast])
        self.assertEqual(fast.ci, ("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=error", "-L",
                                   tests["fast"]["filter"]["include"]["label"]))
        self.assertEqual(tests["fast"]["execution"]["noTestsAction"], "error")
        suites = table["SUITES"]
        self.assertEqual(set(suites), {"windows-full", "windows-fuzz", "linux-full", "linux-fuzz-targets"})
        self.assertEqual({n: [s.ci for s in suites[n][1]] for n in ("linux-full", "linux-fuzz-targets")},
                         {"linux-full": [("ctest", "--preset", "full")],
                          "linux-fuzz-targets": [("ctest", "--preset", "fuzz")]})
        trees = self.loop.TREES
        self.assertEqual(table["READS"], {
            "linux-full": {"build/" + tests["full"]["configurePreset"]: trees["linux-plain"]["build/ci"]},
            "linux-fuzz-targets": {"build/" + tests["fuzz"]["configurePreset"]: trees["linux-fuzz"]["build/ci"]}})
        self.assertEqual([s.ci for s in suites["windows-full"][1]],
                         [("cmake", "--preset", "structure"), ("cmake", "--build", "--preset", "structure"),
                          ("cmake", "--preset", "nohal"), ("cmake", "--build", "--preset", "nohal"),
                          ("ctest", "--preset", "full")])
        self.assertEqual([s.ci for s in suites["windows-fuzz"][1]],
                         [("cmake", "--preset", "fuzz"), ("cmake", "--build", "--preset", "fuzz"),
                          ("ctest", "--preset", "fuzz")])
        self.assertEqual(tests["full"]["configurePreset"], "release")
        windows = [n for n, j in self.loop.JOBS.items() if j[0] == "windows"] + ["windows-full", "windows-fuzz"]
        linux = [n for n, j in self.loop.JOBS.items() if j[0] == "linux"]
        self.assertEqual(set(trees), {*windows, *linux})
        for names in (windows, linux):
            owned = [tree for name in names for tree in trees[name].values()]
            self.assertEqual(len(owned), len(set(owned)), owned)
            self.assertNotIn("build/ci", owned)
        self.assertEqual({name: trees[name] for name in linux}, {name: {"build/ci": "build/" + name} for name in linux})

    def test_environment(self):
        for name, (system, _, _) in self.ci_jobs().items():
            ci_env = self.ci[name][2]
            for key, value in self.loop.ENV[system].items():
                if key in ci_env:
                    with self.subTest(job=name, key=key):
                        self.assertEqual(value, ci_env[key])
            for key in ci_env:
                if key.endswith("_OPTIONS") or key == "CTEST_PARALLEL_LEVEL":
                    with self.subTest(job=name, key=key):
                        self.assertIn(key, self.loop.ENV[system])

    def test_draft(self):
        """No job of either workflow runs for a draft pull request; marking
        it ready runs them."""
        for name in ("ci.yml", "msan.yml"):
            with self.subTest(workflow=name):
                self.assertEqual(draft_findings((WORKFLOWS / name).read_text(encoding="utf-8")), [])

    def test_draft_planted(self):
        """Copies of each workflow, each without the condition on one job or
        without the ready event, are caught."""
        condition = "    if: " + DRAFT + "\n"
        ready = ", ready_for_review]"
        for name in ("ci.yml", "msan.yml"):
            text = (WORKFLOWS / name).read_text(encoding="utf-8")
            jobs = blocks(blocks(text.splitlines(), 0)["jobs"][1:], 2)
            self.assertEqual(text.count(condition), len(jobs))
            self.assertEqual(text.count(ready), 1)
            plants = {"no ready event": text.replace(ready, "]"),
                      "no types": text.replace(text[text.index("    types: "):text.index(ready) + len(ready) + 1], ""),
                      "every job runs": text.replace(condition, "")}
            for job in jobs:
                head = "  " + job + ":\n" + condition
                plants[f"{job} runs"] = text.replace(head, "  " + job + ":\n")
                plants[f"{job} kept by another if"] = text.replace(head, "  " + job + ":\n    if: ${{ always() }}\n")
            for plant, planted in plants.items():
                with self.subTest(workflow=name, plant=plant):
                    self.assertNotEqual(planted, text)
                    self.assertNotEqual(draft_findings(planted), [])

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
