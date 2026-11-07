"""The answer, by the test framework's design: one JSON object on one line,
v2's envelope fields, within its bound, never passed: 8,192 bytes, a refusal's
4,096. Its fixed part is never cut, each line clipped by python's answer; the
reds come next, the first RED_FULL in full and every further one counted under
its configuration and its module; the other lists fill the rest, each at most
LIST_MOST entries, in LISTS' order, `more` counting what was cut. A gate's
answer is one envelope, `data.steps` an entry a step, its code the worst
step's. This directory's README states each member.
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


def red_action(red, owners, command=COMMAND):
    """The next action of a kept red, the gate named that owns it and lists it."""
    gate = f"{command} {owner(red, owners)}"
    # The gate and the ask first, so that the clip at 300 code points never cuts them.
    if red.get("build") is not None:
        return action("RED_KEPT", f"{gate}, the red ending when that tree builds: the build of {red['preset']} "
                                  f"failed since {red['since']}: {red['build']}")
    if red["outcome"] == "flaky":
        return action("TESTS_FLAKY", f"{gate} --again test:{red['test']}, after a look at it live and its fix: "
                                     f"flaky in {red['preset']}, {red['flips']} flips, since {red['since']}")
    return action("RED_KEPT", f"{gate} --again test:{red['test']}, fixed outside its key or its cause gone: "
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


def bounded_line(code, next_action, operation_id, detail_ref, fixed, reds, lists):
    """The answer's one line, within its bound: `fixed`, data's fixed part,
    never cut; `reds`, the red entries in their order; `lists`, {name: entries}
    of LISTS. The reds past RED_FULL, or past the room, are counted in
    data.red_more, {preset: {module: count}}; a list's entries past LIST_MOST
    or the room, and every list after the first cut, are counted in data.more."""
    bound = REFUSAL_BOUND if code in REFUSALS else BOUND
    envelope = {"status": STATUS[code], "code": code, "next_action": next_action and bounded.clip(next_action),
                "operation_id": operation_id, "detail_ref": detail_ref, "data": _clipped(fixed)}
    data = envelope["data"]
    pending = {name: [_clipped(e) for e in lists.get(name, ())] for name in LISTS}
    reds = [_clipped(r) for r in reds]

    def more():  # what is not yet in, every pending entry counted: an upper bound of the final count
        return {name: len(entries) for name, entries in pending.items() if entries}

    def counted(rest):
        found = {}
        for red in rest:
            by = found.setdefault(red["preset"], {})
            by[_module(red)] = by.get(_module(red), 0) + 1
        return found

    data["red"], data["red_more"] = [], {}
    data.update({name: [] for name in LISTS})  # every key in from the first trial, as in the final line
    for place, red in enumerate(reds):
        rest = reds[place + 1:]
        trial = {**envelope, "data": {**data, "red": data["red"] + [red], "red_more": counted(rest), "more": more()}}
        if place >= RED_FULL or _bytes(trial) > bound:
            data["red_more"] = counted(reds[place:])
            break
        data["red"].append(red)
    for red in data["red"]:
        red.pop("modules", None)
        red.pop("labels", None)
    full = False  # the room is spent: this list and every one after are cut
    for name in LISTS:
        entries = pending[name]
        for place, entry in enumerate(entries):
            pending[name] = entries[place + 1:]
            trial = {**envelope, "data": {**data, name: data[name] + [entry], "more": more()}}
            full = full or _bytes(trial) > bound
            if full or place >= LIST_MOST:
                pending[name] = entries[place:]
                break
            data[name].append(entry)
    data["more"] = more()
    line = json.dumps(envelope, ensure_ascii=False, separators=(",", ":"))
    if len(line.encode("utf-8")) > bound:  # the fixed part alone past the bound: named, never passed
        envelope["data"] = {"reason": "the answer's fixed part passed its bound; the run's log names all"}
        line = json.dumps(envelope, ensure_ascii=False, separators=(",", ":"))
    return line


_COUNTS = ("passed", "skipped", "failed", "flaky", "not_started")


def answer(run, steps, reach=None):
    """(the gate's one line, its exit status) of its steps' answers, each
    run.step's dictionary, in their order, at the run id `run`: its code the
    worst step's, or NO_TESTS where an ask names no test of any step (each
    step's `unmatched`), its next action and detail_ref that step's; the counts
    summed; every red of each configuration once, a configuration's own step's
    over another step's reading of it; each list in step order, its entries
    named by their configuration where the gate has more than one step;
    `reach`, {preset: why} of each step its reach left out, in data.reach."""
    nowhere = [a for a in steps[0].get("unmatched", ()) if all(a in s.get("unmatched", ()) for s in steps)]
    code = worst([s["code"] for s in steps] + (["NO_TESTS"] if nowhere else []))
    first = next((s for s in steps if s["code"] == code), None)
    if first is None:  # the gate's own NO_TESTS: no step that ran lists an ask
        first = {**steps[0], "next_action": action("NO_TESTS", f"--again {', '.join(nowhere)} names no test of "
                                                               f"any step of the gate")}
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
    many = len(steps) > 1
    lists = {name: [f"{s['preset']}: {e}" if many else e for s in steps for e in s["lists"].get(name, ())]
             for name in LISTS}

    def summed(field, names):
        return {n: sum(s[field][n] for s in steps) for n in names}

    left_out = summed("left_out", ("tests", "green_at_key", "red_at_key"))
    changed = list(dict.fromkeys(module for s in steps if s["left_out"]["tests"] for module in s["changed"]))
    modules = "no module" if not left_out["tests"] else \
        "every module" + (f" but {', '.join(changed)}" if changed else "")

    fixed = {"reason": first["reason"], "store": "new" if any(s["store"] == "new" for s in steps) else None,
             "plan": any(s["plan"] for s in steps), "build": {"steps": sum(s["build"]["steps"] for s in steps)},
             "selected": {**summed("selected", ("tests", "unknown")),
                          "seconds": round(sum(s["selected"]["seconds"] for s in steps), 1),
                          "expected": round(sum(s["selected"]["expected"] for s in steps), 1)},
             "left_out": {"modules": modules, **left_out},
             "counts": summed("counts", _COUNTS),
             "steps": [{"preset": s["preset"], "code": s["code"], "seconds": s["seconds"],
                        "counts": {"selected": s["selected"]["tests"], **s["counts"],
                                   "left_out": s["left_out"]["tests"]}} for s in steps]}
    if reach:
        fixed["reach"] = dict(reach)
    detail = first["journal"] or next((s["journal"] for s in steps if s["journal"]), None)
    spelled = f"run:{run}"
    line = bounded_line(code, first["next_action"], spelled, detail or f"runtime://operation/{spelled}", fixed,
                        reds, lists)
    return line, exit_status(code)
