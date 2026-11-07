"""A test's record, its state and colour, why its key selects it, and each
transition of the states' table of the test framework's design; the tests
gone, and a failed build's red, its configuration's. A flake is a red, and a
skip ends no red. This directory's README states the table as it is built.
"""

from foundation.devkit.test.gate import junit

GREEN, RED = "green", "red"


def colour(record):
    """The record's colour: a flake is red."""
    return GREEN if record["outcome"] in junit.GREEN and not record["flaky"] else RED


def state(record):
    """Its last outcome, or flaky."""
    return "flaky" if record["flaky"] else record["outcome"]


def is_red(record):
    return colour(record) == RED


def reason(record, key, tree):
    """Why the key selects the test: new, changed or rebuilt; None at its
    key and its tree, where it is left out unless asked."""
    if record is None:
        return "new"
    if record["key"] != key:
        return "changed"
    if record["tree"] != tree:
        return "rebuilt"
    return None


def after(record, key, tree, code, run, ran):
    """The record after `ran`, a junit.Ran, ran at `run` with `key`, `tree`
    and the code id `code`: a new dictionary; the record itself, no
    transition, for a NOT_STARTED, and for a skip of a red record, a flake
    among them, at any key: a red ends only by a passing run."""
    if ran.outcome == junit.NOT_STARTED or (ran.outcome == "skipped" and record is not None and is_red(record)):
        return record
    green = ran.outcome in junit.GREEN
    new = {"key": key, "tree": tree, "code": code, "outcome": ran.outcome, "seconds": ran.seconds}
    if record is None or record["key"] != key:
        return {**new, "flaky": False, "since": run, "flips": record["flips"] if record else 0}
    same = record["code"] == code
    was = state(record)
    flips, since, flaky = record["flips"], record["since"], False
    if was in junit.GREEN:
        if not green:
            since, flips = run, flips + (1 if same else 0)
    elif was == "flaky":
        if green:
            flaky = same
            since = since if same else run
        else:
            flips += 1 if same else 0
    elif green:  # a red
        if same:
            flaky, flips = True, flips + 1
        else:
            since = run
    return {**new, "flaky": flaky, "since": since, "flips": flips}


def gone(tests, listed):
    """The names of the records in `tests` that `listed` names no more, sorted."""
    return sorted(set(tests) - set(listed))


def build_failed(state_file, run, lines):
    """The configuration's state, a state.json's dictionary, given its failed
    build's red at `run` with its lines: its since kept where it was red already."""
    since = state_file["build"]["since"] if state_file.get("build") else run
    state_file["build"] = {"since": since, "run": run, "lines": list(lines)}


def build_passed(state_file):
    """Its tree built: the failed build's red, if any, ends."""
    state_file["build"] = None
