"""A gates file and what it decides, by the test framework's design: its data
checked (each gate's steps, their selection, repeat and reach, the gate that
owns each configuration), each step as run.Step, each configuration's
owners in order, and whether a step's reach leaves it out: no file of its
reach changed since its configuration's last run and no ask names a test it
lists. The file itself, tools/gates.json, and its command line,
tools/gate.py, are the tools module's. This directory's README states each
member.
"""

import json
from pathlib import Path

from foundation.devkit.python import table as module_table
from foundation.devkit.test.gate import envelope, run, selection, store

FORMAT = 1
STEP_FIELDS = {"preset", "source", "select", "labels", "pattern", "repeat", "reach"}
REACH_FIELDS = {"paths", "trees", "suffixes"}
SOURCES = (".", "foundation")
SELECTS = ("changed", "all")


class BadGates(Exception):
    """A gates file unreadable or not in its form: one line."""


def _strings(value, where):
    if not isinstance(value, list) or not value or not all(isinstance(v, str) and v for v in value):
        raise BadGates(f"{where}: not a list of strings")
    return value


def _reach(value, where):
    """A step's reach checked: paths, a path or a directory ending in /; trees,
    names of trees.json's lists of trees; suffixes, a suffix or a name of
    trees.json's lists of suffixes, with trees."""
    if not isinstance(value, dict) or not value or set(value) - REACH_FIELDS:
        raise BadGates(f"{where}: a reach holds paths, or trees with suffixes")
    for field, names in value.items():
        _strings(names, f"{where}.{field}")
        if field != "paths":
            bad = [n for n in names if not n.startswith(".") and n not in module_table.Trees._fields]
            if bad:
                raise BadGates(f"{where}.{field}: no list of trees.json: {', '.join(bad)}")
    if ("trees" in value) != ("suffixes" in value):
        raise BadGates(f"{where}: trees and suffixes go together")
    return value


def load(path):
    """The gates file's data at path, checked: each gate's steps, its first without a reach."""
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise BadGates(f"{path}: {error}") from error
    if not isinstance(data, dict) or data.get("format") != FORMAT or not isinstance(data.get("toolset"), str):
        raise BadGates(f"{path}: not format {FORMAT} with a toolset")
    gates, owners = data.get("gates"), data.get("owners")
    if not isinstance(gates, dict) or not gates or not isinstance(owners, dict):
        raise BadGates(f"{path}: no gates or no owners")
    for preset, gate in owners.items():
        if gate not in gates:
            raise BadGates(f"{path}: owners.{preset} names no gate: {gate}")
    for name, gate in gates.items():
        steps = gate.get("steps") if isinstance(gate, dict) else None
        if not isinstance(steps, list) or not steps:
            raise BadGates(f"{path}: {name} has no steps")
        for number, one in enumerate(steps):
            where = f"{name}.steps[{number}]"
            if not isinstance(one, dict) or set(one) - STEP_FIELDS or not isinstance(one.get("preset"), str):
                raise BadGates(f"{where}: a step holds a preset and only {', '.join(sorted(STEP_FIELDS))}")
            if one.get("select") not in SELECTS or one.get("source", ".") not in SOURCES:
                raise BadGates(f"{where}: select is one of {SELECTS}, source one of {SOURCES}")
            for field in ("labels", "pattern"):
                if field in one and not isinstance(one[field], str):
                    raise BadGates(f"{where}.{field}: not a regular expression")
            repeat = one.get("repeat", 1)
            if not isinstance(repeat, int) or isinstance(repeat, bool) or repeat < 1:
                raise BadGates(f"{where}.repeat: not a count")
            if "reach" in one:
                if number == 0:
                    raise BadGates(f"{where}: a gate's first step has no reach")
                _reach(one["reach"], f"{where}.reach")
    return data


def steps_of(gate, root):
    """[(run.Step, its reach or None)] of a gate's data, each step's targets
    its build preset's in root's presets files."""
    return [(run.Step(one["preset"], one.get("source", "."), one["select"] == "all", one.get("labels"),
                      one.get("pattern"), one.get("repeat"), run.targets(root, one["preset"])), one.get("reach"))
            for one in gate["steps"]]


def owners_of(data):
    """{preset: [(gate, labels, pattern)]}: of the gate the data's owners name
    for the preset, then of envelope.OWNER, each one's step of that preset, so
    that a red's next action names the first whose step lists it."""
    found = {}
    for preset in {p for gate in data["gates"].values() for p in (one["preset"] for one in gate["steps"])}:
        for gate in dict.fromkeys((data["owners"].get(preset, envelope.OWNER), envelope.OWNER)):
            for one in data["gates"].get(gate, {}).get("steps", ()):
                if one["preset"] == preset:
                    found.setdefault(preset, []).append((gate, one.get("labels"), one.get("pattern")))
                    break
    return found


def matcher(reach, trees):
    """Whether a path, from the root, is in reach: by name, under a path of it
    ending in /, or under one of its trees, trees.json's, with one of its
    suffixes."""
    paths = tuple(reach.get("paths", ()))
    roots = tuple(f"{tree}/" for name in reach.get("trees", ())
                  for tree in ((lambda v: (v,) if isinstance(v, str) else v)(getattr(trees, name))))
    suffixes = tuple(s for name in reach.get("suffixes", ())
                     for s in ((name,) if name.startswith(".") else getattr(trees, name)))
    return lambda path: (path in paths or path.startswith(tuple(p for p in paths if p.endswith("/")))
                         or (bool(roots) and path.startswith(roots) and path.endswith(suffixes)))


def _ids(modules, unowned):
    return {path: blob for held in (*modules.values(), unowned) for path, blob in held.items()}


def _asks_listed(one, tests, asks):
    """Whether an ask of asks names a test that the step one lists, of tests,
    its configuration's state.json records, each test's labels its record's:
    known before the step's build, a test with no record yet unknown."""
    return any(envelope.lists((one.labels, one.pattern), name, record.get("labels"))
               and selection.asked(each, name, record.get("labels") or (), record)
               for name, record in tests.items() for each in asks)


def out_of_reach(root, one, reach, files, trees, asks=()):
    """Why the step one, a run.Step, is left out by its reach, or None where
    it runs: no store, an unreadable one (the step answers it), a kept build
    red in its state.json (its build not null), whatever its reach and asks,
    so that its build runs and the change that fixes it, wherever it lies,
    ends the red (H-1), a file of its reach changed since its configuration's
    last run (files', key.Files, blob ids against its state.json's), or an
    ask of asks naming a test it lists by its store's records: an ask adds
    to the selection whatever the step's reach."""
    try:
        state = store.read(store.place(root, one.preset))
    except store.Internal:
        return None
    if state is None or state.get("build") is not None:
        return None
    now, before = _ids(files.modules, files.unowned), _ids(state["modules"], state["unowned"])
    held = matcher(reach, trees)
    if any(held(p) for p in set(now) | set(before) if now.get(p) != before.get(p)):
        return None
    if _asks_listed(one, state["tests"], asks):
        return None
    words = f"left out by its reach: no file of it changed since {state.get('run') or 'its last run'}"
    return words + (", no ask names a test it lists" if asks else "")
