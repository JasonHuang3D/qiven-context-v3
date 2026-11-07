"""The selection, the ask and the plan, by the test framework's design: every
test with no record at its key (new, changed), every test of another tree
(rebuilt) and every asked test (asked); the rest left out at its key, counted
by its colour, never as passed. The plan's expected time is each selected
test's last duration, timing alone. This directory's README states each member.
"""

from collections import namedtuple

from foundation.devkit.test.gate import states

ASKS = ("test", "module", "red")  # an ask's kinds: test:<name>, module:<name>, red
ASKED = "asked"

# An ask: its kind, one of ASKS, and its name, None for red.
Ask = namedtuple("Ask", "kind name")

# A selection: selected, {name: its reason} in the listing's order; left_out,
# {name: {"colour", "since"}}; unmatched, the asks' texts that name no test.
Selection = namedtuple("Selection", "selected left_out unmatched")

# The plan of a selection: its tests, its expected seconds, and how many of
# them have no duration yet, counted apart.
Plan = namedtuple("Plan", "tests seconds unknown")


def ask(text):
    """The Ask that text spells, test:<name>, module:<name> or red; ValueError,
    naming the forms, for any other."""
    kind, sep, name = text.partition(":")
    if text == "red":
        return Ask("red", None)
    if sep and kind in ("test", "module") and name:
        return Ask(kind, name)
    raise ValueError(f"--again {text!r}: is none of test:<name>, module:<name>, red")


def spelled(one):
    """An Ask as it is written on the command line."""
    return one.kind if one.name is None else f"{one.kind}:{one.name}"


def _asked(one, name, labels, record):
    if one.kind == "test":
        return name == one.name
    if one.kind == "module":
        return one.name in labels
    return record is not None and states.is_red(record)


def choose(listed, records, keys, tree, asks=(), every=False):
    """The Selection of the tests `listed`, {name: labels}, by their `records`,
    a state.json's tests, at their `keys`, {name: key}, in the tree of id
    `tree`; `asks`, Asks, add to it and narrow nothing; `every`, a step's
    select all, asks every test."""
    selected, left_out = {}, {}
    hit = {one: False for one in asks}
    for name, labels in listed.items():
        record = records.get(name)
        why = states.reason(record, keys[name], tree)
        for one in asks:
            if _asked(one, name, labels, record):
                hit[one] = True
                why = why or ASKED
        why = why or (ASKED if every else None)
        if why:
            selected[name] = why
        else:
            left_out[name] = {"colour": states.colour(record), "since": record["since"]}
    return Selection(selected, left_out, [spelled(one) for one, found in hit.items()
                                          if not found and one.kind != "red"])


def plan(names, records, jobs):
    """The Plan of the tests `names` by each one's last duration in `records`
    at any key: the larger of their sum over `jobs` and the longest one."""
    known = [records[n]["seconds"] for n in names if n in records and records[n].get("seconds") is not None]
    seconds = max(sum(known) / max(jobs, 1), max(known, default=0.0)) if known else 0.0
    return Plan(len(names), round(seconds, 1), len(names) - len(known))
