"""Each selected test's outcome, read from CTest's JUnit, by the test
framework's design (its D-6): one test, one invocation, one outcome. A test that did not start
is NOT_STARTED, no outcome and no colour: the tool's or the runner's fault,
never the test's. This directory's README states the reading row by row.
"""

import re
import xml.etree.ElementTree as ElementTree
from collections import namedtuple

OUTCOMES = ("passed", "skipped", "timeout", "crashed", "failed")
GREEN = ("passed", "skipped")
NOT_STARTED = "not_started"
SKIPS = ("SKIP_RETURN_CODE=125", "SKIP_REGULAR_EXPRESSION_MATCHED")  # CTest's messages of a skip
_HANG = "124"  # the watcher's status past a case's limit (python's status table)
_ABORT = "3"  # the C runtime's abort
# The watcher's status line: "status: <value> ...", the value a number, 0x and 8 hex digits, or "signal <n>".
_STATUS = re.compile(r"status: (signal \d+|0x[0-9A-Fa-f]{8}|\d+)(?:\s|$)")

Ran = namedtuple("Ran", "outcome seconds why")


def outcome(status, message, last):
    """(outcome, why) of one testcase: its status attribute, its message,
    trimmed, and its output's last line that is not blank."""
    if status == "run":
        return "passed", ""
    if status == "notrun":
        return ("skipped", message) if message in SKIPS else (NOT_STARTED, f"not run: {message or 'no message'}")
    if status != "fail":
        return NOT_STARTED, f"status {status or 'none'}"
    value = _STATUS.match(last)
    value = value.group(1) if value else None
    if message == "Timeout" or value == _HANG:
        return "timeout", message if message == "Timeout" else last
    if message == "SEGFAULT" or message.startswith("Exit code 0x"):
        return "crashed", message
    if value is not None and (value.startswith(("0x", "signal")) or value == _ABORT):
        return "crashed", last
    return "failed", last or message


def read(path, selected):
    """{name: Ran} for each name of `selected`, from the JUnit file at path."""
    try:
        cases = {case.get("name"): case for case in ElementTree.parse(path).getroot().iter("testcase")}
    except (OSError, ElementTree.ParseError) as error:
        return {name: Ran(NOT_STARTED, None, f"no JUnit: {error}") for name in selected}
    ran = {}
    for name in selected:
        case = cases.get(name)
        if case is None:
            ran[name] = Ran(NOT_STARTED, None, "no testcase in the JUnit")
            continue
        status = case.get("status", "")
        told = case.find("skipped" if status == "notrun" else "failure")
        message = (told.get("message", "") if told is not None else "").strip()
        lines = [line.strip() for line in (case.findtext("system-out") or "").splitlines() if line.strip()]
        found, why = outcome(status, message, lines[-1] if lines else "")
        if found == "skipped":  # the reason the case printed, "SKIP <name>: <reason>", else CTest's message
            said = f"SKIP {name}: "
            why = next((line[len(said):] for line in lines if line.startswith(said)), why)
        ran[name] = Ran(found, _seconds(case.get("time")), why)
    return ran


def _seconds(text):
    """A testcase's time, a float, or None where it is none."""
    try:
        return float(text)
    except (TypeError, ValueError):
        return None
