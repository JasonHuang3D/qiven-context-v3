"""Print each Python test of the checkout for CTest, of the packages of the
trees tools/trees.json walks and of its layers' trees: its id, its labels,
serial, and its own limit.

Run from the checkout's root as python -I foundation/devkit/test/runner/list_tests.py
[--trees <tree>...] [--leave-out <name>...]: --trees, the trees it walks, by default
every one; --leave-out, tests it does not list, each by its id or a package's name.
Each line is "<id>\t<labels by commas>\t<0 or 1>\t<seconds or nothing>". A
test's labels are its package's, by its whole name where PACKAGE_LABELS
names it so, or its module's where MODULE_LABELS names it, below, and those
its class names in ctest_labels, then the name of the
module of the table its file is in (the tree's module.json files, read by
foundation/devkit/python/table.py), by which the runner selects it
(tools/changed.py); a class whose ctest_serial is true runs alone; a
class whose ctest_limit is a whole number of seconds states the watcher's
limit for its tests, which otherwise their labels give
(ctest_tests.cmake, beside this file).

A module that does not import, a test the one reader of test code
(package(), below) says is no test code or puts in a package with no labels
here, a file in no module, a limit of another form, or a test directory with no
__init__.py, which discover would pass over, fails the listing, and so the
build.
"""

import argparse
import sys
import unittest
from pathlib import Path

# Under -I the checkout is off sys.path; appended, so that the standard library still wins.
sys.path.append(str(Path(__file__).resolve().parents[4]))
from foundation.devkit.python import table as module_table

# The labels of each package of tests/ that holds Python tests.
PACKAGE_LABELS = {
    "drive": ("drive",),
    "contract": ("drive",),
    "slow": ("slow",),
    "machine": ("machine",),
    "bench": ("bench",),
    "drill": ("drill",),
    "tests": ("drive",),  # a module's own tests/ under a layer's tree
    "foundation.devkit.structure.tests": ("fast",),  # by its whole name: the structure method's own tests/
}
# A module, by its whole name, whose labels are its own, in place of its
# package's: the analysis tools' controls, which run on the analysis build alone.
MODULE_LABELS = {
    "foundation.devkit.structure.tests.test_analysis": ("analysis",),
}


def walk(suite):
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from walk(test)
        else:
            yield test


def packages(root, trees):
    """The top packages under the trees, from the checkout's root, sorted:
    each directory holding an __init__.py whose parent holds none."""
    found = []
    for tree in trees:
        for init in sorted((Path(root) / tree).rglob("__init__.py")):
            package = init.parent
            if "__pycache__" not in package.parts and not (package.parent / "__init__.py").is_file():
                found.append(package)
    return sorted(set(found))


def unpackaged(root, trees):
    """The directories under the trees, from the checkout's root, sorted,
    holding a file of discover's pattern and no __init__.py: discover would
    pass over their tests without a word."""
    found = {path.parent for tree in trees for path in (Path(root) / tree).rglob("test*.py")}
    return sorted(d for d in found if "__pycache__" not in d.parts and not (d / "__init__.py").is_file())


def package(path, trees, homes):
    """The package whose labels a Python test file at path takes, by the one
    reader of test code (the Trees' test_path, homes its module
    directories): the one below tests/, or tests for a file directly in a
    module's own tests/; None where the reader says it is no test code."""
    test = trees.test_path(path, homes)
    parts = (test or "").split("/")
    return parts[1] if len(parts) > 2 else "tests" if test and test != path else None


def listed(root=".", kept=None, left_out=()):
    """([(id, labels, serial, limit or None)], [errors]) of the
    tests discover finds in each package of the kept trees, by default the
    trees tools/trees.json walks and its layers', from the checkout's root;
    but those left_out names, each by its id or a package's name."""
    table = module_table.load(root)
    trees = table["trees"]
    kept = trees.every if kept is None else kept
    homes = {home.rpartition("/")[0] for home in table["homes"].values()}
    loader = unittest.TestLoader()
    suite = unittest.TestSuite(loader.discover(str(package), top_level_dir=str(root))
                               for package in packages(root, kept))
    rows, errors = [], [f"{d.relative_to(root).as_posix()}: a test directory with no __init__.py"
                        for d in unpackaged(root, kept)]
    for test in walk(suite):
        test_id = test.id()
        if f"{test_id}.".startswith(tuple(f"{name}." for name in left_out)):
            continue
        error = getattr(test, "_exception", None)
        name = test_id.rsplit(".", 2)[0]  # the test's module
        if error is not None or not name.startswith(tuple(f"{tree}." for tree in kept)):
            errors.append(f"{test_id}: {error}")
            continue
        parts = name.split(".")
        path = "/".join(parts) + ".py"
        kind = package(path, trees, homes)
        if kind not in PACKAGE_LABELS:
            errors.append(f"{test_id}: the package {kind} has no labels" if kind else f"{test_id}: no test code")
            continue
        own = PACKAGE_LABELS.get(name.rpartition(".")[0], PACKAGE_LABELS[kind])
        labels = MODULE_LABELS.get(name, own) + tuple(getattr(test, "ctest_labels", ()))
        serial = 1 if getattr(test, "ctest_serial", False) else 0
        limit = getattr(test, "ctest_limit", None)
        if limit is not None and (type(limit) is not int or limit < 1):
            errors.append(f"{test_id}: ctest_limit {limit!r} is no whole number of seconds")
            continue
        owner = module_table.owners(table, [path])[path]
        if not owner:
            errors.append(f"{test_id}: its file is in no module of the table")
            continue
        rows.append((test_id, labels + (owner[0],), serial, limit))
    return rows, errors + [str(error) for error in loader.errors]


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--trees", nargs="+")
    parser.add_argument("--leave-out", nargs="+", default=())
    args = parser.parse_args(argv)
    rows, errors = listed(kept=args.trees, left_out=args.leave_out)
    for test_id, labels, serial, limit in rows:
        print(f"{test_id}\t{','.join(labels)}\t{serial}\t{'' if limit is None else limit}")
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
