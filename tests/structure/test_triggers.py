"""CI at publication alone: each workflow under .github/workflows runs on a
pull request into main, on a release tag and by manual dispatch, and on
nothing else: never a push to a branch, never a schedule, never a pull
request into another branch. A newer run cancels the older of its ref, and
no job is kept off one of those events. Read from each workflow's text,
and over planted workflows that break each rule."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
EVENTS = {
    "pull_request": {"branches": ["main"]},
    "push": {"tags": ["v*"]},
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
        head = "on:\n  pull_request:\n    branches: [main]\n  push:\n    tags: [\"v*\"]\n  workflow_dispatch:\n"
        self.assertEqual(text.count(head), 1)
        plants = {
            "push to a branch": head.replace("    tags:", "    branches: [main]\n    tags:"),
            "bare push": head.replace("  push:\n    tags: [\"v*\"]\n", "  push:\n"),
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
