"""The answer, by the test framework's design: one JSON object on one line,
v2's envelope fields, within its bound, never passed: 8,192 bytes, a refusal's
4,096. Its fixed part is never cut, each line clipped by python's answer; the
reds come next, each with its ask, the first RED_FULL in full and every further
one counted under its configuration and its module, past the room under its
configuration alone, none dropped; the other lists fill the rest, each at most
LIST_MOST entries, in LISTS' order, `more` counting what was cut. A gate's
answer is one envelope, `data.steps` an entry a step, its preset and tree; its
code and next action from the gate's reds as they stand after every step, each
step's own code first. This directory's README states each member.
"""

import json
import re

from foundation.devkit.python import answer as bounded

# Each code in its order, the first that applies the answer's: (code, status, next action's verb).
CODES = (
    ("INTERNAL", "failed", "report"),
    ("NOT_STARTED", "failed", "look"),
    ("INTERRUPTED", "failed", "rerun"),
    ("TOOL_MISSING", "not_ready", "prepare"),
    ("TABLE_ERROR", "not_ready", "fix"),
    ("STORE_LOCKED", "conflict", "wait"),
    ("BUILD_FAILED", "failed", "fix"),
    ("NO_TESTS", "missing", "fix"),
    ("TESTS_FAILED", "failed", "fix"),
    ("TESTS_FLAKY", "failed", "look"),
    ("RED_KEPT", "failed", "fix"),
    ("PLANNED", "pending", "rerun"),  # a plan's selection, none of it run: not ok, so never read as green
    ("PASSED", "ok", None),
    ("NOTHING_TO_RUN", "ok", None),
)
ORDER = {code: place for place, (code, _, _) in enumerate(CODES)}
STATUS = {code: status for code, status, _ in CODES}
VERB = {code: verb for code, _, verb in CODES}
REFUSALS = ("TOOL_MISSING", "TABLE_ERROR", "STORE_LOCKED")
BOUND, REFUSAL_BOUND = 8192, 4096
RED_FULL = 20  # the reds named in full; every further one counted
LIST_MOST = 20  # a list's entries at most
LISTS = ("faults", "build_lines", "changed", "unowned", "untested", "skipped", "gone")
COMMAND = "py -3.15 -I tools/gate.py"
OWNER = "presets"  # the gate that owns a configuration no one-tree gate names


def worst(codes):
    """The first of codes in CODES' order."""
    return min(codes, key=ORDER.__getitem__)


def exit_status(code):
    """0 for an ok answer, 1 for any other."""
    return 0 if STATUS[code] == "ok" else 1


def lists(filters, test, labels):
    """Whether a step of filters, (labels, pattern), its regular expressions or
    None, lists the test of that name and labels: labels None, unread, lists
    under no labels filter alone."""
    by_labels, by_name = filters
    if by_labels and (labels is None or not any(re.search(by_labels, label) for label in labels)):
        return False
    return not (by_name and not re.search(by_name, test))


def owner(red, owners):
    """The gate that owns a red: of owners' entries for its configuration,
    [(gate, labels, pattern)] in order, the first whose step lists its test, a
    build's red the first; else OWNER."""
    for gate, by_labels, by_name in owners.get(red["preset"], ()):
        if red.get("build") is not None or lists((by_labels, by_name), red["test"], red.get("labels")):
            return gate
    return OWNER


def action(code, words):
    """The next action of code, `<verb>: <words>`, None for an ok code."""
    return None if VERB[code] is None else bounded.clip(f"{VERB[code]}: {words}")


def ask(red, owners, command=COMMAND):
    """The ask of a kept red: the gate that owns it and lists it, a test's red
    with its `--again`."""
    gate = f"{command} {owner(red, owners)}"
    return gate if red.get("build") is not None else f"{gate} --again test:{red['test']}"


def red_action(red, owners=None, command=COMMAND):
    """The next action of a kept red: its entry's ask, else the one of the gate
    that owns it and lists it."""
    asked = red.get("ask") or ask(red, owners or {}, command)
    # The gate and the ask first, so that the clip at 300 code points never cuts them.
    if red.get("build") is not None:
        return action("RED_KEPT", f"{asked}, the red ending when that tree builds: the build of {red['preset']} "
                                  f"failed since {red['since']}: {red['build']}")
    if red["outcome"] == "flaky":
        return action("TESTS_FLAKY", f"{asked}, after a look at it live and its fix: "
                                     f"flaky in {red['preset']}, {red['flips']} flips, since {red['since']}")
    return action("RED_KEPT", f"{asked}, fixed outside its key or its cause gone: "
                              f"{red['outcome']} in {red['preset']} since {red['since']}, its output in {red['log']}")


def _bytes(value):
    return len(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def _clipped(value):
    """value with each string in it clipped to a line's bound."""
    if isinstance(value, str):
        return bounded.clip(value)
    if isinstance(value, list):
        return [_clipped(v) for v in value]
    if isinstance(value, dict):
        return {k: _clipped(v) for k, v in value.items()}
    return value


def _module(red):
    return (red.get("modules") or ["no module"])[0]


def _counted(reds, alone=False):
    """The reds counted, {preset: {module: count}}, or {preset: count} alone."""
    found = {}
    for red in reds:
        if alone:
            found[red["preset"]] = found.get(red["preset"], 0) + 1
        else:
            by = found.setdefault(red["preset"], {})
            by[_module(red)] = by.get(_module(red), 0) + 1
    return found


def bounded_line(code, next_action, operation_id, detail_ref, fixed, reds, lists, more=None, weights=None):
    """The answer's one line, within its bound: `fixed`, data's fixed part,
    never cut; `reds`, the red entries in their order; `lists`, {name: entries}
    of LISTS; `more`, {name: count} of a list's entries cut before, counted in
    data.more; `weights`, {name: [count]}, what each entry of a list counts in
    data.more when cut, 1 where absent. The reds past RED_FULL, or past the
    room, are counted in data.red_more, {preset: {module: count}}, or, where
    those counts pass the room, {preset: count}; a list's entries past
    LIST_MOST or the room, and every list after the first cut, are counted in
    data.more."""
    bound = REFUSAL_BOUND if code in REFUSALS else BOUND
    envelope = {"status": STATUS[code], "code": code, "next_action": next_action and bounded.clip(next_action),
                "operation_id": operation_id, "detail_ref": detail_ref, "data": _clipped(fixed)}
    data, before, weights = envelope["data"], dict(more or {}), weights or {}
    pending = {}
    for name in LISTS:
        entries = [_clipped(e) for e in lists.get(name, ())]
        pending[name] = list(zip(entries, weights.get(name) or [1] * len(entries)))
    reds = [_clipped(r) for r in reds]

    def cut():  # what is not yet in, every pending entry counted: an upper bound of the final count
        found = {name: before.get(name, 0) + sum(weight for _, weight in entries) for name, entries in pending.items()}
        return {name: count for name, count in found.items() if count}

    def fits(**fields):
        return _bytes({**envelope, "data": {**data, "more": cut(), **fields}}) <= bound

    data["red"], data["red_more"] = [], {}
    data.update({name: [] for name in LISTS})  # every key in from the first trial, as in the final line
    for place, red in enumerate(reds):
        rest, trial = reds[place + 1:], data["red"] + [red]
        if place >= RED_FULL or not (fits(red=trial, red_more=_counted(rest))
                                     or fits(red=trial, red_more=_counted(rest, alone=True))):
            break
        data["red"].append(red)
    rest = reds[len(data["red"]):]
    data["red_more"] = _counted(rest)
    if not fits():  # the counts by module past the room: by configuration alone, none dropped
        data["red_more"] = _counted(rest, alone=True)
    for red in data["red"]:
        red.pop("modules", None)
        red.pop("labels", None)
    full = False  # the room is spent: this list and every one after are cut
    for name in LISTS:
        entries = pending[name]
        for place, (entry, _) in enumerate(entries):
            pending[name] = entries[place + 1:]
            full = full or not fits(**{name: data[name] + [entry]})
            if full or place >= LIST_MOST:
                pending[name] = entries[place:]
                break
            data[name].append(entry)
    data["more"] = cut()
    line = json.dumps(envelope, ensure_ascii=False, separators=(",", ":"))
    if len(line.encode("utf-8")) > bound:  # the fixed part alone past the bound: named, every red counted
        envelope["data"] = {"reason": "the answer's fixed part passed its bound; the run's log names all",
                            "red_more": _counted(reds, alone=True)}
        line = json.dumps(envelope, ensure_ascii=False, separators=(",", ":"))
    return line


_COUNTS = ("passed", "skipped", "failed", "flaky", "not_started")


_OF_REDS = ("RED_KEPT", "PLANNED", "PASSED", "NOTHING_TO_RUN")  # a step's codes that its reading of reds decides


def _own(step):
    """A step's code of its own run, the reds it read aside: its code, or, of
    _OF_REDS, PLANNED, PASSED or NOTHING_TO_RUN by its plan and selection."""
    if step["code"] not in _OF_REDS:
        return step["code"]
    if not step["selected"]["tests"]:
        return "NOTHING_TO_RUN"
    return "PLANNED" if step["plan"] else "PASSED"


def answer(run, steps, reach=None):
    """(the gate's one line, its exit status) of its steps' answers, each
    run.step's dictionary, in their order, at the run id `run`: every red of
    each configuration once, a configuration's own step's over another step's
    reading of it, so the reds as they stand after every step; its code the
    first in CODES' order of each step's own (`_own`), RED_KEPT where a red
    stands, and NO_TESTS where an ask names no test of any step (each step's
    `unmatched`), never the worst step's: a red a later step ended fails no
    gate; its next
    action the first red's on RED_KEPT, else that of the first step of its
    code, whose reason and journal are the gate's, the reason counting every
    red; the counts summed; each list in step order, its entries named by
    their configuration where the gate has more than one step, each step's
    `more` and the skipped list's skips counted in data.more; `reach`,
    {preset: why} of each step its reach left out, in data.reach."""
    stepped = {s["preset"] for s in steps}
    reds, read = [], set()
    for each in steps:
        reds += each["red"]
    for each in reversed(steps):  # another configuration's reds as the latest step read them
        for preset, found in each["others"].items():
            if preset not in stepped and preset not in read:
                read.add(preset)
                reds += found
    reds.sort(key=lambda r: not r.get("ran"))
    nowhere = [a for a in steps[0].get("unmatched", ()) if all(a in s.get("unmatched", ()) for s in steps)]
    code = worst([_own(s) for s in steps] + (["RED_KEPT"] if reds else []) + (["NO_TESTS"] if nowhere else []))
    first = next((s for s in steps if s["code"] == code), None) or next((s for s in steps if _own(s) == code), None)
    if code == "RED_KEPT":
        first, next_action = first or steps[0], red_action(reds[0])
    elif first is None:  # the gate's own NO_TESTS: no step that ran lists an ask
        first, next_action = steps[0], action("NO_TESTS", f"--again {', '.join(nowhere)} names no test of "
                                                          f"any step of the gate")
    elif first["code"] == code:
        next_action = first["next_action"]
    else:  # a plan whose reds read are gone: its own words were a red's
        next_action = action(code, f"{first['selected']['tests']} tests, about {first['selected']['expected']} s: "
                                   f"the gate again without --plan")
    many = len(steps) > 1
    lists = {name: [f"{s['preset']}: {e}" if many else e for s in steps for e in s["lists"].get(name, ())]
             for name in LISTS}
    weights = {"skipped": [int(e.split(":", 1)[0]) for s in steps for e in s["lists"].get("skipped", ())]}
    more = {name: sum(s.get("more", {}).get(name, 0) for s in steps) for name in LISTS}

    def summed(field, names):
        return {n: sum(s[field][n] for s in steps) for n in names}

    left_out = summed("left_out", ("tests", "green_at_key", "red_at_key"))
    changed = list(dict.fromkeys(module for s in steps if s["left_out"]["tests"] for module in s["changed"]))
    modules = "no module" if not left_out["tests"] else \
        "every module" + (f" but {', '.join(changed)}" if changed else "")

    reason = "; ".join(part for part in (first["reason"], f"{len(reds)} red kept" if reds else "") if part)
    fixed = {"reason": reason, "store": "new" if any(s["store"] == "new" for s in steps) else None,
             "plan": any(s["plan"] for s in steps), "build": {"steps": sum(s["build"]["steps"] for s in steps)},
             "selected": {**summed("selected", ("tests", "unknown")),
                          "seconds": round(sum(s["selected"]["seconds"] for s in steps), 1),
                          "expected": round(sum(s["selected"]["expected"] for s in steps), 1)},
             "left_out": {"modules": modules, **left_out},
             "counts": summed("counts", _COUNTS),
             "steps": [{"preset": s["preset"], "tree": s["tree"], "code": s["code"], "seconds": s["seconds"],
                        "counts": {"selected": s["selected"]["tests"], **s["counts"],
                                   "left_out": s["left_out"]["tests"]}} for s in steps]}
    if reach:
        fixed["reach"] = dict(reach)
    detail = first["journal"] or next((s["journal"] for s in steps if s["journal"]), None)
    spelled = f"run:{run}"
    line = bounded_line(code, next_action, spelled, detail or f"runtime://operation/{spelled}", fixed, reds, lists,
                        more=more, weights=weights)
    return line, exit_status(code)
