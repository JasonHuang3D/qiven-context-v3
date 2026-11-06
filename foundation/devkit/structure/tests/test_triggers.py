"""CI by manual dispatch alone: each workflow under .github/workflows runs
by workflow_dispatch and on nothing else: never a pull request, a tag, a
push to a branch or a schedule. No job or step is guarded by its event: no
draft guard, no if on the event's name. A later dispatch never cancels a
run in progress: each workflow's concurrency group, and any job's, names
the workflow and the commit, and none cancels in progress.

CI a runtime evaluation, unpinned (Unpinned below): each Linux job on
ubuntu-24.04, named by that label, each Windows and macOS job on its
runner's latest image, and no other label; no digest, exact version or
versioned tool in a workflow or a CI preset, no system setting changed, no
debugger case run, and each job printing its image and its tools' versions.
Read from each workflow's and CMakePresets.json's text."""

import json
import re
import unittest

from foundation.devkit.python.build_tree import ROOT

WORKFLOWS = ROOT / ".github" / "workflows"
EVENTS = {"workflow_dispatch": {}}
EVENT_GUARD = re.compile(r"github\.event\.pull_request|\.draft\b")
# What a concurrency group must name: the workflow, so that two workflows of
# one commit never share it, and the commit, so that no run of another
# commit does.
GROUP_NAMES = ("github.workflow", "github.sha")


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


def concurrency_findings(where, conc):
    """Each way a concurrency mapping lets a later dispatch cancel a run in
    progress: a group that does not name the workflow and the commit, or a
    cancel-in-progress but false (absent is false)."""
    found = []
    group = conc.get("group")
    names = re.findall(r"\$\{\{(.*?)\}\}", group) if isinstance(group, str) else []
    for name in GROUP_NAMES:
        if not any(re.search(r"(?<![\w.])" + re.escape(name) + r"(?![\w.])", expr) for expr in names):
            found.append(f"{where}: group {group!r} does not name {name}")
    cancel = conc.get("cancel-in-progress")
    if cancel not in (None, "false"):
        found.append(f"{where}: cancel-in-progress {cancel!r}")
    return found


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
    found += concurrency_findings("concurrency", mapping(conc))
    _, jobs = block(lines, "jobs")
    for job, body in sorted(mapping(jobs).items()):
        if isinstance(body, dict) and "github.event" in str(body.get("if", "")):
            found.append(f"job {job} kept off an event")
        if isinstance(body, dict) and "concurrency" in body:
            own = body["concurrency"]
            found += concurrency_findings(f"job {job}'s concurrency", own if isinstance(own, dict) else {"group": own})
    for number, line in enumerate(lines, 1):
        if not line.lstrip().startswith("#") and EVENT_GUARD.search(line.split(" #", 1)[0]):
            found.append(f"line {number}: a draft guard")
    return found


# The one runner label of each system's jobs, by the job's name: Linux on
# the release the retired local Linux pins named, Windows and macOS on their latest.
LABELS = {"windows": "windows-latest", "linux": "ubuntu-24.04", "macos": "macos-latest"}
# What each job prints, by its label: the image, CPython, CMake, CTest and
# Ninja, and the compilers; on Linux glibc as well, ldd's first line.
PRINTS = ("ImageOS", "ImageVersion", "sys.version", "cmake --version", "ctest --version", "ninja --version")
COMPILER_PRINTS = {"windows-latest": ("cl.exe", "VCToolsVersion", "WindowsSDKVersion"),
                   "ubuntu-24.04": ("clang++", "--version", "ldd --version"),
                   "macos-latest": ("clang++ --version", "sw_vers")}
VERSION = re.compile(r"(?<![\w.])\d+\.\d+(?:\.\d+)*(?![\w.])")
DIGEST = re.compile(r"(?<![0-9a-fA-F])[0-9a-fA-F]{40,}(?![0-9a-fA-F])")
VERSIONED_TOOL = re.compile(r"(?<![\w-])(gcc|g\+\+|clang|clang\+\+|clang-tidy|llvm-[a-z]+|cl)-\d+")
PINNING = re.compile(r"sha256sum\s+(-c|--check)|shasum\b.*\s-c\b|Get-FileHash|certutil|-vcvars_ver|"
                     r"QIVEN_PIN_TOOLS|version_info\s*\[")
SETTING = re.compile(r"\bsysctl\b|/proc/sys/|\breg(\.exe)?\s+add\b|\b(Set|New)-ItemProperty\b|\bbcdedit\b|"
                     r"\bdefaults\s+write\b|\bpowercfg\b|\bSet-MpPreference\b|\bupdate-alternatives\b")
CTEST_PRESETS = {"fake", "ci-analysis"}
STRUCTURE_ONLY = "^foundation[.]devkit[.]structure[.]"


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
    a debugger case, runs on a label but its system's or prints no versions."""
    found = []
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split(" #", 1)[0] if not line.lstrip().startswith("#") else ""
        if not code.strip() or re.match(r"^\s*(- )?uses:", code):
            continue
        label = re.fullmatch(r"\s*runs-on:\s*(\S+)\s*", code)
        if label and label.group(1) in LABELS.values():
            continue  # the label, its release's number, checked by its job below
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
        system = job.split("-", 1)[0]
        if len(runs) != 1 or runs[0] != LABELS.get(system):
            found.append(f"job {job}: runs on {runs}, not {LABELS.get(system, 'a system named by the job')}")
            continue
        missing = [token for token in PRINTS + COMPILER_PRINTS[runs[0]] if token not in body]
        if missing:
            found.append(f"job {job}: prints no {', '.join(missing)}")
    return found


def read_presets(path):
    """The presets file at path with those of each file it includes, as CMake reads them."""
    data = json.loads(path.read_bytes().decode("utf-8"))
    for name in data.get("include", ()):
        included = read_presets(path.parent / name)
        for kind in ("configurePresets", "buildPresets", "testPresets"):
            data.setdefault(kind, []).extend(included.get(kind, ()))
    return data


def preset_findings(data):
    """Each way the presets read (read_presets) pin a CI preset's tool, or let
    CI's test presets take a debugger case."""
    configure ={p["name"]: p for p in data["configurePresets"]}

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
            if key == "QIVEN_PIN_TOOLS" or "VERSION" in key:
                found.append(f"preset {name}: sets {key}")
            if key.endswith("_COMPILER") and not re.fullmatch(r"cl|gcc|g\+\+|clang|clang\+\+", value):
                found.append(f"preset {name}: {key} {value}, not the image's")
        words = f"{preset.get('displayName', '')} {preset.get('description', '')}"
        images = set(re.findall(r"(?<![\w-])(?:windows|ubuntu|macos)-\w+(?:\.\w+)*", words)) - set(LABELS.values())
        if images or VERSIONED_TOOL.search(words):
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


class Unpinned(unittest.TestCase):
    """CI a runtime evaluation: the tree clean."""

    def setUp(self):
        self.ci = (WORKFLOWS / "ci.yml").read_bytes().decode("utf-8")
        self.msan = (WORKFLOWS / "msan.yml").read_bytes().decode("utf-8")
        self.presets = read_presets(ROOT / "CMakePresets.json")

    def test_tree(self):
        for path in workflows():
            with self.subTest(workflow=path.name):
                self.assertEqual(unpinned_findings(path.read_bytes().decode("utf-8")), [])
        self.assertEqual(preset_findings(self.presets), [])
        self.assertEqual(sorted(job_blocks(self.ci)), ["linux", "windows"])
        self.assertEqual(sorted(job_blocks(self.msan)), ["linux-msan"])
