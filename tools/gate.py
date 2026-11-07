"""The test gate's command line, by the test framework's design: it runs a gate of tools/gates.json, each of its
steps in order, a step a configuration's build, its selection and its ctest,
by the module foundation/devkit/test/gate (its README holds the method), and
prints one answer, a JSON envelope on one line. A step with a reach runs
only where a file of its reach changed since its configuration's last run;
the answer names each step its reach left out.

    py -3.15 -I tools/gate.py <gate> [--again <what>]... [--plan]
    py -3.15 -I tools/gate.py --help

<what> is test:<name>, module:<name> or red, repeatable: it adds to the
selection and narrows nothing. --plan builds and selects, runs no test, and
answers the selection's count and its expected time. Exit 0 for an answer
whose status is ok, 1 for any other, 2 for a usage error, its usage on
standard error and no answer. QIVEN_VCVARS names vcvars64.bat where it is
not found. QIVEN_CDB names the pinned cdb, a tool as vcvars is: unset, or
naming a file off its pin, each step answers TOOL_MISSING, no test run.
"""

import argparse
import json
import os
import sys
from pathlib import Path

# Under -I the checkout is off sys.path; appended, so that the standard library still wins.
sys.path.append(str(Path(__file__).resolve().parents[1]))
from foundation.devkit.python import git
from foundation.devkit.python import system
from foundation.devkit.python import table as module_table
from foundation.devkit.python.build_tree import ROOT
from foundation.devkit.test.gate import envelope, key, run, selection, store

GATES = ROOT / "tools" / "gates.json"
FORMAT = 1
STEP_FIELDS = {"preset", "source", "select", "labels", "pattern", "repeat", "reach"}
REACH_FIELDS = {"paths", "trees", "suffixes"}
SOURCES = (".", "foundation")
SELECTS = ("changed", "all")


class BadGates(Exception):
    """tools/gates.json unreadable or not in its form: one line."""


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


def load(path=GATES):
    """gates.json's data, checked: each gate's steps, its first without a reach."""
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


def steps_of(gate, root=ROOT):
    """[(run.Step, its reach or None)] of a gate's data, each step's targets its build preset's."""
    return [(run.Step(one["preset"], one.get("source", "."), one["select"] == "all", one.get("labels"),
                      one.get("pattern"), one.get("repeat"), run.targets(root, one["preset"])), one.get("reach"))
            for one in gate["steps"]]


def owners_of(data):
    """{preset: [(gate, labels, pattern)]}: of the gate gates.json's owners name
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


def _matcher(reach, trees):
    """Whether a path, from the root, is in reach."""
    paths = tuple(reach.get("paths", ()))
    roots = tuple(f"{tree}/" for name in reach.get("trees", ())
                  for tree in ((lambda v: (v,) if isinstance(v, str) else v)(getattr(trees, name))))
    suffixes = tuple(s for name in reach.get("suffixes", ())
                     for s in ((name,) if name.startswith(".") else getattr(trees, name)))
    return lambda path: (path in paths or path.startswith(tuple(p for p in paths if p.endswith("/")))
                         or (bool(roots) and path.startswith(roots) and path.endswith(suffixes)))


def _ids(modules, unowned):
    return {path: blob for held in (*modules.values(), unowned) for path, blob in held.items()}


def out_of_reach(root, preset, reach, files, trees):
    """Why the step of preset is left out by its reach, or None where it runs:
    no store, an unreadable one (the step answers it), or a file of its reach
    changed since its configuration's last run (git's blob ids against its
    state.json's)."""
    try:
        state = store.read(store.place(root, preset))
    except store.Internal:
        return None
    if state is None:
        return None
    now, before = _ids(files.modules, files.unowned), _ids(state["modules"], state["unowned"])
    held = _matcher(reach, trees)
    if any(held(p) for p in set(now) | set(before) if now.get(p) != before.get(p)):
        return None
    return f"left out by its reach: no file of it changed since {state.get('run') or 'its last run'}"


def _faulted(words):
    """The answer of a gate that could not be read: INTERNAL, its reason."""
    spelled = store.spelled(store.run_id())
    line = envelope.bounded_line("INTERNAL", envelope.action("INTERNAL", words), spelled,
                                 f"runtime://operation/{spelled}", {"reason": words, "steps": []}, [], {})
    return line, envelope.exit_status("INTERNAL")


class _Tools:
    """The tools found once a run, Missing kept for every step that asks."""

    def __init__(self, version):
        self.version, self.found, self.missing = version, None, None

    def __call__(self):
        if self.found is None and self.missing is None:
            try:
                self.found = run.find_tools(self.version, os.environ.get("QIVEN_VCVARS"))
            except run.Missing as missing:
                self.missing = missing
        if self.missing is not None:
            raise self.missing
        return run.Runner(self.found)


def _parser(gates):
    parser = argparse.ArgumentParser(prog="py -3.15 -I tools/gate.py", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("gate", choices=sorted(gates) or None, help="the gate of tools/gates.json to run")
    parser.add_argument("--again", action="append", default=[], metavar="WHAT", type=selection.ask,
                        help="test:<name>, module:<name> or red: run them again; repeatable")
    parser.add_argument("--plan", action="store_true", help="build and select, run no test")
    return parser


def main(argv):
    try:
        data, fault = load(), None
    except BadGates as bad:
        data, fault = {"gates": {}}, str(bad)
    args = _parser(data["gates"]).parse_args(argv)  # a usage error: its usage on stderr, exit 2
    if fault:
        line, status = _faulted(fault)
        print(line)
        return status
    system.kill_on_close()  # before any start of vcvars, cmake, ninja or ctest: each dies with this run
    name, owners = args.gate, owners_of(data)
    steps, run_id = steps_of(data["gates"][name]), store.run_id()
    tools, known = _Tools(data["toolset"]), run.presets(ROOT)
    files = trees = None
    if any(reach for _, reach in steps):
        try:
            table = module_table.load(ROOT)
            files, trees = key.files(ROOT, table), table["trees"]
        except (OSError, ValueError, git.GitFailed):
            files = None  # every reach step runs, and its own step answers the table or git
    answers, left = [], {}
    for one, reach in steps:
        why = out_of_reach(ROOT, one.preset, reach, files, trees) if reach and files is not None else None
        if why:
            left[one.preset] = why
            continue
        answers.append(run.step(ROOT, one, tools, run=run_id, gate=name, argv=sys.argv, asks=args.again,
                                plan=args.plan, owners=owners, known=known, alone=len(steps) == 1))
    line, status = envelope.answer(run_id, answers, reach=left)
    print(line)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
