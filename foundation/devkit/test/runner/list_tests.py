"""Print each Python test of tests/ for CTest: its id, its labels, serial,
and its own limit.

Run from the checkout's root as python -I foundation/devkit/test/runner/list_tests.py
[--loop]. The loop's own tests, of a class whose ctest_git is true, those
that start git, are listed with --loop alone, which one build a system
gives (QIVEN3_LOOP_TESTS), labelled loop and fake alone, so that the git
they start is started in one step a run, that build's fake step; there
every Python test runs behind the counting shim (tools/loop_shim.py),
which fails a test of no such class that starts git. Each
line is "<id>\t<labels by commas>\t<0 or 1>\t<seconds or nothing>". A
test's labels are its package's, or its module's where MODULE_LABELS names
it, below, and those its class names in ctest_labels, then the name of the
module of the table its file is in (the tree's module.json files, read by
tools/loop_modules.py), by which the loop selects it
(tools/loop_select.py); a class whose ctest_serial is true runs alone; a
class whose ctest_limit is a whole number of seconds states the watcher's
limit for its tests, which otherwise their labels give
(ctest_tests.cmake, beside this file).

A module that does not import, a test in a package with no labels here or
a file in no module, or a limit of another form fails the listing, and so
the build.
"""

import runpy
import sys
import unittest
from pathlib import Path

# The labels of each package of tests/ that holds Python tests.
PACKAGE_LABELS = {
    "drive": ("drive",),
    "contract": ("drive",),
    "structure": ("fast",),
    "slow": ("slow",),
    "machine": ("machine",),
    "bench": ("bench",),
    "drill": ("drill",),
}
# A module whose labels are its own, in place of its package's: the analysis
# tools' controls, which run on the analysis build alone.
MODULE_LABELS = {
    "structure.test_analysis": ("analysis",),
}
# The labels of the loop's own tests, in place of any other: the fake step
# selects them, no other step of the loop's build.
GIT_LABELS = ("loop", "fake")


def walk(suite):
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from walk(test)
        else:
            yield test


def listed(loop, root="."):
    """([(id, labels, serial, limit or None)], [errors]) of the
    tests discover finds under tests/ of the checkout's root; the loop's
    own with loop alone."""
    loader = unittest.TestLoader()
    suite = loader.discover(str(Path(root) / "tests"), top_level_dir=str(root))
    rows, errors = [], []
    # The table's one reader, by runpy: under -I tools/ is off sys.path.
    modules = runpy.run_path(str(Path(root) / "tools" / "loop_modules.py"))
    table = modules["load"](root)
    for test in walk(suite):
        test_id = test.id()
        error = getattr(test, "_exception", None)
        if error is not None or not test_id.startswith("tests."):
            errors.append(f"{test_id}: {error}")
            continue
        git = getattr(test, "ctest_git", False)
        if type(git) is not bool:
            errors.append(f"{test_id}: ctest_git {git!r} is neither true nor false")
            continue
        if git and not loop:
            continue
        package = test_id.split(".")[1]
        if package not in PACKAGE_LABELS:
            errors.append(f"{test_id}: the package {package} has no labels")
            continue
        module = ".".join(test_id.split(".")[1:3])
        labels = MODULE_LABELS.get(module, PACKAGE_LABELS[package]) + tuple(getattr(test, "ctest_labels", ()))
        serial = 1 if getattr(test, "ctest_serial", False) else 0
        limit = getattr(test, "ctest_limit", None)
        if limit is not None and (type(limit) is not int or limit < 1):
            errors.append(f"{test_id}: ctest_limit {limit!r} is no whole number of seconds")
            continue
        path = "/".join(test_id.split(".")[:3]) + ".py"
        owner = modules["owners"](table, [path])[path]
        if not owner:
            errors.append(f"{test_id}: its file is in no module of the table")
            continue
        rows.append((test_id, (GIT_LABELS if git else labels) + (owner[0],), serial, limit))
    return rows, errors + [str(error) for error in loader.errors]


def main(argv):
    if argv not in ([], ["--loop"]):
        print(f"usage: list_tests.py [--loop], not {argv}", file=sys.stderr)
        return 2
    rows, errors = listed(bool(argv))
    for test_id, labels, serial, limit in rows:
        print(f"{test_id}\t{','.join(labels)}\t{serial}\t{'' if limit is None else limit}")
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
