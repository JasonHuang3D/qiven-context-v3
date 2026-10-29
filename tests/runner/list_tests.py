"""Print each Python test of tests/ for CTest: its id, its labels, serial,
its own limit, and what it reads at run time.

Run from the checkout's root as python -I tests/runner/list_tests.py
[--loop]. The loop's own tests, the modules of the counting shim's groups
(tests/drive/test_git_shim.py's GROUPS), are never listed: they run only
under the shim, whose tests are listed with --loop alone, which one build a
system gives (QIVEN3_LOOP_TESTS), so that the git they start is started in
one step a run. Each
line is "<id>\t<labels by commas>\t<0 or 1>\t<seconds or nothing>\t<reads by
commas>". A test's labels are its package's, or its module's where
MODULE_LABELS names it, below, and those its class names in ctest_labels; a
class whose ctest_serial is true runs alone; a class whose ctest_limit is a
whole number of seconds states the watcher's limit for its tests, which
otherwise their labels give (runner/ctest_tests.cmake).

A class's ctest_reads declares what its tests read at run time beyond the
modules they import, for the loop's affected tests (tools/loop_select.py):
each a binary of the build by its target's name, a name with no "/" and no
".", or a file or directory of the checkout by its path from the root, a
directory's ending in "/", "." the whole checkout.

A module that does not import, a test in a package with no labels here, or
a limit or a read of another form fails the listing, and so the build.
"""

import re
import sys
import unittest

# A read of ctest_reads: ".", a target's name, or a path of the checkout from
# its root, its parts named plainly and none of them "." or "..".
READ = re.compile(r"\.|(?:(?!\.\.?/)[A-Za-z0-9_.+-]+/)*(?:(?!\.\.?$)[A-Za-z0-9_.+-]+)?")

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


def walk(suite):
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from walk(test)
        else:
            yield test


def main(argv):
    if argv not in ([], ["--loop"]):
        print(f"usage: list_tests.py [--loop], not {argv}", file=sys.stderr)
        return 2
    loader = unittest.TestLoader()
    suite = loader.discover("tests", top_level_dir=".")
    from tests.drive import test_git_shim  # importable once discover has put the root on sys.path
    unlisted = test_git_shim.modules() | (set() if argv else {test_git_shim.__name__})
    failed = False
    for test in walk(suite):
        test_id = test.id()
        error = getattr(test, "_exception", None)
        if error is not None or not test_id.startswith("tests."):
            print(f"{test_id}: {error}", file=sys.stderr)
            failed = True
            continue
        if ".".join(test_id.split(".")[:3]) in unlisted:
            continue
        package = test_id.split(".")[1]
        if package not in PACKAGE_LABELS:
            print(f"{test_id}: the package {package} has no labels", file=sys.stderr)
            failed = True
            continue
        module = ".".join(test_id.split(".")[1:3])
        labels = MODULE_LABELS.get(module, PACKAGE_LABELS[package]) + tuple(getattr(test, "ctest_labels", ()))
        serial = 1 if getattr(test, "ctest_serial", False) else 0
        limit = getattr(test, "ctest_limit", None)
        if limit is not None and (type(limit) is not int or limit < 1):
            print(f"{test_id}: ctest_limit {limit!r} is no whole number of seconds", file=sys.stderr)
            failed = True
            continue
        reads = getattr(test, "ctest_reads", ())
        if type(reads) is not tuple or not all(type(r) is str and r and READ.fullmatch(r) for r in reads):
            print(f"{test_id}: ctest_reads {reads!r} is no tuple of target names and checkout paths",
                  file=sys.stderr)
            failed = True
            continue
        print(f"{test_id}\t{','.join(labels)}\t{serial}\t{'' if limit is None else limit}\t{','.join(reads)}")
    for error in loader.errors:
        print(error, file=sys.stderr)
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
