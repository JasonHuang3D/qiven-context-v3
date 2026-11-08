"""CTest's wrapper: a tree's tests listed from CTest's JSON, a selection run
and its JUnit read back, each answer decoded whole in one place; its fake.
See this directory's README.md.
"""

import json
import os
import xml.etree.ElementTree as ElementTree
from collections import namedtuple
from pathlib import Path

from qiven_devkit.wrap import call
from qiven_devkit.wrap.fake import Fake

LISTING, RUN = "ctest listing", "ctest run"
_TESTFILE = "CTestTestfile.cmake"
_STATUSES = ("run", "fail", "notrun", "disabled")  # a JUnit testcase's status, in CTest's words


class Test(namedtuple("Test", ("command", "properties"))):
    """A test of the listing: its command, a list of str or None; its
    properties, {name: value}, as CTest's JSON holds them."""

    __slots__ = ()


class Result(namedtuple("Result", ("status", "message", "time"))):
    """A test's result in the JUnit: its status in CTest's words, the message
    of its failure or skip (None where it has none), its time in seconds."""

    __slots__ = ()


class Ran(namedtuple("Ran", ("status", "results", "absent"))):
    """A run: CTest's status; {name: Result} of each test its JUnit holds;
    `absent`, each name asked that the JUnit does not hold."""

    __slots__ = ()


class NoTestFile(call.CallError):
    """The tree holds no CTestTestfile.cmake."""

    answer = "no test file"


class NoTests(call.CallError):
    """None of the names a run asked is a test of the tree."""

    answer = "no tests"


class CTest:
    """CTest, as `program` names it, run under `env` with `limit` seconds a call."""

    def __init__(self, program, env, limit):
        self.program, self.env, self.limit = program, env, limit

    def listing(self, tree):
        """{name: Test} of every test of tree, from CTest's JSON listing."""
        tree = _tree(LISTING, tree)
        answer = self._call(LISTING, tree, ["--show-only=json-v1"])
        try:
            data = json.loads(answer.out.decode("utf-8"))
        except ValueError as error:  # UnicodeDecodeError is a ValueError
            raise call.Unreadable(LISTING, f"not UTF-8 JSON: {type(error).__name__}: {error}") from None
        if not isinstance(data, dict) or data.get("kind") != "ctestInfo" or \
                not isinstance(data.get("version"), dict) or data["version"].get("major") != 1 or \
                not isinstance(data.get("tests"), list):
            raise call.Unreadable(LISTING, "not a ctestInfo listing of major version 1")
        tests = {}
        for test in data["tests"]:
            name, command, properties = _test(test)
            if name in tests:
                raise call.Unreadable(LISTING, f"the test {name!r} twice")
            tests[name] = Test(command, properties)
        return tests

    def run(self, tree, names, *, junit, jobs, repeat=None, log=None):
        """Ran of the tests names of tree, jobs at once, until-fail repeat
        times where given; their JUnit in the file junit, CTest's output in
        the file log where given; NoTests where none of names is a test."""
        tree = _tree(RUN, tree)
        names = list(dict.fromkeys(names))
        for name in names:
            if not isinstance(name, str) or not name or "\n" in name or "\r" in name:
                raise ValueError(f"{RUN}: a test's name is empty or holds a line break: {name!r}")
        for number, what in ((jobs, "jobs"), (repeat, "repeat")):
            if (number is not None or what == "jobs") and \
                    (isinstance(number, bool) or not isinstance(number, int) or number < 1):
                raise ValueError(f"{RUN}: {what} must be a whole number above 0, not {number!r}")
        junit = _absolute(junit)
        asked = junit.with_name(junit.name + ".names")
        asked.write_bytes("".join(f"{name}\n" for name in names).encode("utf-8"))
        if junit.exists():
            junit.unlink()  # a JUnit left from an earlier run is never read as this one's
        args = ["--tests-from-file", str(asked), "--output-junit", str(junit), "-j", str(jobs)]
        if repeat is not None:
            args += ["--repeat", f"until-fail:{repeat}"]
        if log is not None:
            args += ["--output-log", str(_absolute(log))]
        answer = self._call(RUN, tree, args, takes=(0, 8))
        results = _junit(junit, set(names))
        if not results:
            raise NoTests(RUN, f"none of the {len(names)} name(s) asked is a test of {tree}: "
                               f"{', '.join(names[:10])}")
        return Ran(answer.status, results, [name for name in names if name not in results])

    def _call(self, name, tree, args, takes=(0,)):
        return call.call(name, self.program, ["--test-dir", str(tree), "--no-tests=error", *args],
                         env=self.env, cwd=tree, limit=self.limit, takes=takes)


class FakeCTest(Fake):
    """CTest's fake: its methods, answered by call name; no CTest started."""

    def listing(self, tree):
        return self._answer(LISTING, tree=tree)

    def run(self, tree, names, *, junit, jobs, repeat=None, log=None):
        return self._answer(RUN, tree=tree, names=list(names), junit=junit, jobs=jobs, repeat=repeat, log=log)


def _tree(name, tree):
    """tree as an absolute Path; NoTestFile where it holds no CTestTestfile.cmake."""
    tree = _absolute(tree, name)
    if not (tree / _TESTFILE).is_file():
        raise NoTestFile(name, f"{tree} holds no {_TESTFILE}: it is no configured tree of tests")
    return tree


def _absolute(path, name=RUN):
    """path as a Path; a ValueError where it is not absolute."""
    path = Path(os.fspath(path))
    if not path.is_absolute():
        raise ValueError(f"{name}: a path must be absolute, not {str(path)!r}")
    return path


def _test(test):
    """(name, command, properties) of one test of the listing, checked."""
    if not isinstance(test, dict) or not isinstance(test.get("name"), str):
        raise call.Unreadable(LISTING, f"a test without a name: {test!r:.200}")
    name, command = test["name"], test.get("command")
    if command is not None and (not isinstance(command, list) or not all(isinstance(w, str) for w in command)):
        raise call.Unreadable(LISTING, f"{name!r}: a command that is not a list of strings: {command!r:.200}")
    properties = {}
    for item in test.get("properties", []):
        if not isinstance(item, dict) or not isinstance(item.get("name"), str) or "value" not in item:
            raise call.Unreadable(LISTING, f"{name!r}: a property without a name and a value: {item!r:.200}")
        if item["name"] in properties:
            raise call.Unreadable(LISTING, f"{name!r}: the property {item['name']!r} twice")
        properties[item["name"]] = item["value"]
    return name, command, properties


def _junit(file, asked):
    """{name: Result} of each testcase of the JUnit file, checked against the
    names asked: the one place CTest's JUnit is read."""
    try:
        root = ElementTree.parse(file).getroot()
    except (OSError, ElementTree.ParseError) as error:
        raise call.Unreadable(RUN, f"its JUnit {file}: {type(error).__name__}: {error}") from None
    if root.tag != "testsuite":
        raise call.Unreadable(RUN, f"its JUnit {file}: the root is <{root.tag}>, not <testsuite>")
    results = {}
    for case in root.iter("testcase"):
        name, status = case.get("name"), case.get("status")
        if name is None or name not in asked:
            raise call.Unreadable(RUN, f"its JUnit holds a test not asked: {name!r}")
        if name in results:
            raise call.Unreadable(RUN, f"its JUnit holds {name!r} twice")
        if status not in _STATUSES:
            raise call.Unreadable(RUN, f"{name!r}: a status not in CTest's words {_STATUSES}: {status!r}")
        try:
            time = float(case.get("time"))
        except (TypeError, ValueError):
            raise call.Unreadable(RUN, f"{name!r}: a time that is no number: {case.get('time')!r}") from None
        said = case.find("failure")
        if said is None:
            said = case.find("skipped")
        results[name] = Result(status, None if said is None else said.get("message"), time)
    return results
