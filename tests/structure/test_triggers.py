"""CI at publication alone: each workflow under .github/workflows runs on a
pull request into main, on a release tag of the version pattern v[0-9]* and
by manual dispatch, and on nothing else: never a push to a branch, never a
schedule, never a pull request into another branch. A newer run cancels the
older of its ref, and no job is kept off one of those events.

CI a runtime evaluation, unpinned (Unpinned below): the
runners' latest images, no digest, exact version or versioned tool in a
workflow or a CI preset, no system setting changed, no debugger case run,
and each job printing its image and its tools' versions. Read from each
workflow's and CMakePresets.json's text, and over planted copies that break
each rule."""

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
EVENTS = {
    "pull_request": {"branches": ["main"]},
    "push": {"tags": ["v[0-9]*"]},
    "workflow_dispatch": {},
}
GROUP = "${{ github.workflow }}-${{ github.ref }}"


def unquote(text):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "'\"":
        return text[1:-1]
    return text


def scalar(text):
    """A value after its key: a flow list, a quoted or plain scalar, or None."""
    text = re.sub(r"\s+#.*$", "", text).strip()
    if not text:
        return None
    if text.startswith("[") and text.endswith("]"):
        return [unquote(part) for part in text[1:-1].split(",") if part.strip()]
    return unquote(text)


def block(lines, key):
    """The lines under a top-level key, and the value on the key's own line."""
    out, inside, value = [], False, None
    for line in lines:
        m = re.match(r"^(['\"]?)([A-Za-z0-9_-]+)\1:(.*)$", line)
        if m:
            inside = m.group(2) == key
            if inside:
                value = scalar(m.group(3))
            continue
        if inside:
            out.append(line)
    return value, out


def mapping(lines):
    """{key: value} of a block's least-indented keys; a key's value is its
    inline scalar, or the list of its block items, or the mapping under it."""
    body = [line for line in lines if line.strip() and not line.lstrip().startswith("#")]
    if not body:
        return {}
    indent = min(len(line) - len(line.lstrip()) for line in body)
    out, key, under = {}, None, []

    def close():
        if key is None:
            return
        items = [line.strip() for line in under]
        if items and all(item.startswith("- ") for item in items):
            out[key] = [unquote(item[2:]) for item in items]
        elif under:
            out[key] = mapping(under)

    for line in body:
        level = len(line) - len(line.lstrip())
        m = re.match(r"^ *(['\"]?)([A-Za-z0-9_-]+)\1:(.*)$", line)
        if level == indent and m:
            close()
            key, under = m.group(2), []
            out[key] = scalar(m.group(3))
        elif level == indent:
            close()
            key, under = None, []
            out.setdefault("", []).append(line.strip())
        else:
            under.append(line)
    close()
    return out


def findings(text):
    """Each way this workflow's text breaks the triggers' rules."""
    lines = text.splitlines()
    found = []
    value, under = block(lines, "on")
    if value is None and not under:
        found.append("no triggers")
    if isinstance(value, str):
        value = [value]
    events = {name: None for name in value} if isinstance(value, list) else mapping(under)
    for name in sorted(set(events) - set(EVENTS)):
        found.append(f"trigger {name}")
    for name, filters in sorted(EVENTS.items()):
        if name not in events:
            found.append(f"no trigger {name}")
            continue
        got = events[name] or {}
        if got != filters:
            found.append(f"trigger {name}: {got} is not {filters}")
    _, conc = block(lines, "concurrency")
    conc = mapping(conc)
    if conc.get("group") != GROUP or conc.get("cancel-in-progress") != "true":
        found.append(f"concurrency {conc}")
    _, jobs = block(lines, "jobs")
    for job, body in sorted(mapping(jobs).items()):
        if isinstance(body, dict) and "event_name" in str(body.get("if", "")):
            found.append(f"job {job} kept off an event")
    return found


IMAGES = {"windows-latest", "ubuntu-latest", "macos-latest"}
# What each job prints, by its image: the image, CPython, CMake, CTest and
# Ninja, and the compilers.
PRINTS = ("ImageOS", "ImageVersion", "sys.version", "cmake --version", "ctest --version", "ninja --version")
COMPILER_PRINTS = {"windows-latest": ("cl.exe", "VCToolsVersion", "WindowsSDKVersion"),
                   "ubuntu-latest": ("clang++", "--version"), "macos-latest": ("clang++ --version", "sw_vers")}
VERSION = re.compile(r"(?<![\w.])\d+\.\d+(?:\.\d+)*(?![\w.])")
DIGEST = re.compile(r"(?<![0-9a-fA-F])[0-9a-fA-F]{40,}(?![0-9a-fA-F])")
VERSIONED_TOOL = re.compile(r"(?<![\w-])(gcc|g\+\+|clang|clang\+\+|clang-tidy|llvm-[a-z]+|cl)-\d+")
PINNING = re.compile(r"sha256sum\s+(-c|--check)|shasum\b.*\s-c\b|Get-FileHash|certutil|-vcvars_ver|"
                     r"QIVEN3_PIN_TOOLS|QIVEN3_COMPILER_LINE|version_info\s*\[")
SETTING = re.compile(r"\bsysctl\b|/proc/sys/|\breg(\.exe)?\s+add\b|\b(Set|New)-ItemProperty\b|\bbcdedit\b|"
                     r"\bdefaults\s+write\b|\bpowercfg\b|\bSet-MpPreference\b|\bupdate-alternatives\b")
CTEST_PRESETS = {"fake", "ci-analysis"}
STRUCTURE_ONLY = "^tests[.]structure[.]"


def job_blocks(text):
    """{job: its lines} of a workflow's jobs, comment lines left out."""
    out, job, inside = {}, None, False
    for line in text.splitlines():
        if re.match(r"^\S", line):
            inside = line.startswith("jobs:")
            continue
        m = re.match(r"^  ([A-Za-z0-9_-]+):\s*$", line)
        if inside and m:
            job = m.group(1)
            out[job] = []
        elif inside and job and not line.lstrip().startswith("#"):
            out[job].append(line)
    return out


def ctest_findings(line):
    """A ctest command that could run a debugger case: a test preset but
    CI's, a label selection that takes the label debugger, or none."""
    found = []
    for m in re.finditer(r"(?:^|[;&|]\s*|\s)ctest\s+(.*)$", line):
        words = []
        for word in re.findall(r"'[^']*'|\"[^\"]*\"|[^\s;|&]+|[;|&]+", m.group(1)):
            if word[0] in ";|&":
                break
            words.append(word.strip("'\""))
        if "--version" in words:
            continue
        options = {w: words[i + 1] for i, w in enumerate(words[:-1]) if w in ("--preset", "-L", "-R")}
        if "--preset" in options and options["--preset"] not in CTEST_PRESETS:
            found.append(f"ctest --preset {options['--preset']}")
        elif "-L" in options and re.search(options["-L"], "debugger"):
            found.append(f"ctest -L {options['-L']} takes the debugger cases")
        elif not options.keys() & {"--preset", "-L"} and options.get("-R") != STRUCTURE_ONLY:
            found.append(f"ctest with no selection: {line.strip()[:80]}")
    return found


def unpinned_findings(text):
    """Each way a workflow's text pins a tool, sets a system setting, runs
    a debugger case, runs on a fixed image or prints no versions."""
    found = []
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split(" #", 1)[0] if not line.lstrip().startswith("#") else ""
        if not code.strip() or re.match(r"^\s*(- )?uses:", code):
            continue
        for rule, pattern in (("a digest", DIGEST), ("an exact version", VERSION), ("a versioned tool", VERSIONED_TOOL),
                              ("a pin", PINNING), ("a system setting", SETTING)):
            if pattern.search(code):
                found.append(f"line {number}: {rule}: {code.strip()[:80]}")
        m = re.match(r"^\s*python-version:\s*(.*)$", code)
        if m and m.group(1).strip().strip("'\"") != "3.x":
            found.append(f"line {number}: python-version {m.group(1).strip()}")
        if "debugger" in code:
            found.append(f"line {number}: names the debugger cases")
        found += [f"line {number}: {f}" for f in ctest_findings(code)]
    for job, lines in sorted(job_blocks(text).items()):
        body = "\n".join(lines)
        runs = re.findall(r"^    runs-on:\s*(\S+)", body, re.MULTILINE)
        if len(runs) != 1 or runs[0] not in IMAGES:
            found.append(f"job {job}: runs on {runs}, not a latest image")
            continue
        missing = [token for token in PRINTS + COMPILER_PRINTS[runs[0]] if token not in body]
        if missing:
            found.append(f"job {job}: prints no {', '.join(missing)}")
    return found


def preset_findings(text):
    """Each way CMakePresets.json pins a CI preset's tool, or lets CI's test
    presets take a debugger case."""
    data = json.loads(text)
    configure = {p["name"]: p for p in data["configurePresets"]}

    def chain(name):
        preset = configure[name]
        parents = preset.get("inherits", [])
        parents = [parents] if isinstance(parents, str) else parents
        return [name] + [n for parent in parents for n in chain(parent)]

    found = []
    for name in sorted(configure):
        if "ci" not in chain(name):
            continue
        preset = configure[name]
        for key, value in sorted(preset.get("cacheVariables", {}).items()):
            value = str(value.get("value", "") if isinstance(value, dict) else value)
            if key in ("QIVEN3_PIN_TOOLS", "QIVEN3_COMPILER_LINE") or "VERSION" in key:
                found.append(f"preset {name}: sets {key}")
            if key.endswith("_COMPILER") and not re.fullmatch(r"cl|gcc|g\+\+|clang|clang\+\+", value):
                found.append(f"preset {name}: {key} {value}, not the image's")
        words = f"{preset.get('displayName', '')} {preset.get('description', '')}"
        if re.search(r"(windows|ubuntu|macos)-\d", words) or VERSIONED_TOOL.search(words):
            found.append(f"preset {name}: names a fixed image or tool: {words[:80]}")
    tests = {p["name"]: p for p in data["testPresets"]}
    for name in sorted(CTEST_PRESETS):
        filters = tests.get(name, {}).get("filter", {})
        include = filters.get("include", {}).get("label", "")
        exclude = filters.get("exclude", {}).get("label", "")
        if not include or re.search(include, "debugger") or (name == "fake" and not re.search(exclude or "(?!)",
                                                                                              "debugger")):
            found.append(f"test preset {name}: takes the debugger cases ({include!r}, excluding {exclude!r})")
    return found


def workflows():
    return sorted(WORKFLOWS.glob("*.y*ml"))


class Triggers(unittest.TestCase):
    def test_tree(self):
        self.assertEqual([p.name for p in workflows()], ["ci.yml", "msan.yml"])
        for path in workflows():
            with self.subTest(workflow=path.name):
                self.assertEqual(findings(path.read_bytes().decode("utf-8")), [])

    def test_planted(self):
        text = (WORKFLOWS / "ci.yml").read_bytes().decode("utf-8")
        head = "on:\n  pull_request:\n    branches: [main]\n  push:\n    tags: [\"v[0-9]*\"]\n  workflow_dispatch:\n"
        self.assertEqual(text.count(head), 1)
        plants = {
            "push to a branch": head.replace("    tags:", "    branches: [main]\n    tags:"),
            "bare push": head.replace("  push:\n    tags: [\"v[0-9]*\"]\n", "  push:\n"),
            "push of any branch but one": head + "  push:\n    branches-ignore: [dev]\n",
            "schedule": head + "  schedule:\n    - cron: \"0 3 * * *\"\n",
            "pull request into another branch": head.replace("[main]", "[dev]"),
            "pull request into any branch": head.replace("    branches: [main]\n", ""),
            "pull request into main and others": head.replace("[main]", "[main, \"release/*\"]"),
            "pull request target": head + "  pull_request_target:\n",
            "no dispatch": head.replace("  workflow_dispatch:\n", ""),
            "flow form": "on: [push, pull_request, workflow_dispatch]\n",
            "scalar form": "on: push\n",
            "quoted key": head.replace("on:", "'on':") + "  schedule:\n    - cron: \"0 3 * * *\"\n",
            "any tag": head.replace("[\"v[0-9]*\"]", "[\"v*\"]"),
        }
        for name, planted in plants.items():
            with self.subTest(plant=name):
                self.assertNotEqual(findings(text.replace(head, planted)), [])
        conc = "  cancel-in-progress: true\n"
        self.assertEqual(text.count(conc), 1)
        group = "  group: " + GROUP + "\n"
        others = {
            "older run kept": text.replace(conc, "  cancel-in-progress: false\n"),
            "group of the branch alone": text.replace(group, "  group: ${{ github.ref }}\n"),
            "job kept off an event": text.replace(
                "    name: macos-clang\n", "    name: macos-clang\n    if: github.event_name == 'pull_request'\n"),
            "no triggers": text.replace(head, ""),
        }
        for name, planted in others.items():
            with self.subTest(plant=name):
                self.assertNotEqual(planted, text)
                self.assertNotEqual(findings(planted), [])


class Unpinned(unittest.TestCase):
    """CI a runtime evaluation: the tree clean, each plant caught."""

    def setUp(self):
        self.ci = (WORKFLOWS / "ci.yml").read_bytes().decode("utf-8")
        self.msan = (WORKFLOWS / "msan.yml").read_bytes().decode("utf-8")
        self.presets = (ROOT / "CMakePresets.json").read_bytes().decode("utf-8")

    def test_tree(self):
        for path in workflows():
            with self.subTest(workflow=path.name):
                self.assertEqual(unpinned_findings(path.read_bytes().decode("utf-8")), [])
        self.assertEqual(preset_findings(self.presets), [])
        self.assertEqual(sorted(job_blocks(self.ci)), ["linux", "macos", "windows"])
        self.assertEqual(sorted(job_blocks(self.msan)), ["linux-msan"])

    def planted(self, text, old, new):
        self.assertEqual(text.count(old), 1, old)
        return text.replace(old, new)

    def test_planted_workflows(self):
        ci, msan = self.ci, self.msan
        tools = "      - name: The image and the tools this job runs\n"
        mac = ci[ci.index("  macos:\n"):]
        mac_tools = mac[mac.index(tools):mac.index("      - name: Configure (macos-clang)")]
        plants = {
            "a digest": (ci, "env:\n  CTEST_PARALLEL_LEVEL",
                         "env:\n  PYTHON_SHA256: " + "67d9a63e" * 8 + "\n  CTEST_PARALLEL_LEVEL"),
            "a digest check": (msan, "| cut -d' ' -f1)\"", "| cut -d' ' -f1)\"; sha256sum -c sums.txt"),
            "an exact version": (ci, "env:\n  CTEST_PARALLEL_LEVEL", "env:\n  LLVM_VERSION: 19.1.7\n  CTEST_PARALLEL_LEVEL"),
            "an exact CPython": (msan, 'python-version: "3.x"', 'python-version: "3.15"'),
            "a versioned compiler": (msan, "-DCMAKE_CXX_COMPILER=clang++ ", "-DCMAKE_CXX_COMPILER=clang++-18 "),
            "a toolset pinned": (ci, 'vcvars64.bat`" >nul', 'vcvars64.bat`" -vcvars_ver=14.44 >nul'),
            "the pins' option": (ci, "cmake --preset ${{ matrix.preset }}\n\n      # -k 0",
                                 "cmake --preset ${{ matrix.preset }} -DQIVEN3_PIN_TOOLS=ON\n\n      # -k 0"),
            "a system setting": (msan, "      # libc++ with libc++abi",
                                 "      - name: Randomisation\n        run: sudo sysctl -w vm.mmap_rnd_bits=28\n\n"
                                 "      # libc++ with libc++abi"),
            "a fixed image": (ci, "runs-on: windows-latest", "runs-on: windows-2022"),
            "a fixed Linux image": (msan, "runs-on: ubuntu-latest", "runs-on: ubuntu-24.04"),
            "the debugger preset": (ci, "run: ctest --preset fake\n\n      - name: Each fuzz",
                                    "run: ctest --preset debugger\n\n      - name: Each fuzz"),
            "the debugger label": (msan, "-L '^cxx$'", "-L '^(cxx|debugger)$'"),
            "the full suite": (ci, "run: ctest --preset fake\n\n      - name: Clean",
                               "run: ctest --preset full\n\n      - name: Clean"),
            "no selection": (ci, "-R '^tests[.]structure[.]' -LE '^analysis$'", "-LE '^analysis$'"),
            "no versions printed": (ci, mac_tools, ""),
            "no Ninja version": (msan, "ninja --version\n", "\n"),
        }
        for name, (text, old, new) in plants.items():
            with self.subTest(plant=name):
                self.assertNotEqual(unpinned_findings(self.planted(text, old, new)), [])

    def test_planted_presets(self):
        text = self.presets
        plants = {
            "the pins' option on a CI preset": ('"CMAKE_C_COMPILER": "cl", "CMAKE_CXX_COMPILER": "cl"}',
                                                '"CMAKE_C_COMPILER": "cl", "CMAKE_CXX_COMPILER": "cl", '
                                                '"QIVEN3_PIN_TOOLS": "ON"}'),
            "a compiler's line": ('"CMAKE_C_COMPILER": "cl", "CMAKE_CXX_COMPILER": "cl"}',
                                  '"CMAKE_C_COMPILER": "cl", "CMAKE_CXX_COMPILER": "cl", "QIVEN3_COMPILER_LINE": "19.44"}'),
            "a versioned compiler": ('"CMAKE_C_COMPILER": "gcc", "CMAKE_CXX_COMPILER": "g++", "CMAKE_BUILD_TYPE": '
                                     '"RelWithDebInfo"}',
                                     '"CMAKE_C_COMPILER": "gcc", "CMAKE_CXX_COMPILER": "g++-14", "CMAKE_BUILD_TYPE": '
                                     '"RelWithDebInfo"}'),
            "a fixed image named": ('"CI on windows-latest, the image\'s MSVC"', '"CI on windows-2022, MSVC 19.44"'),
            "fake takes the debugger cases": ('"include": {"label": "^fake$"}, "exclude": {"label": "^debugger$"}',
                                              '"include": {"label": "^(fake|debugger)$"}'),
            "fake excludes none": ('"include": {"label": "^fake$"}, "exclude": {"label": "^debugger$"}',
                                   '"include": {"label": "^fake$"}'),
        }
        for name, (old, new) in plants.items():
            with self.subTest(plant=name):
                self.assertNotEqual(preset_findings(self.planted(text, old, new)), [])
