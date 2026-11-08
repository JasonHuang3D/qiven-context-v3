"""Print each Python test of the checkout for CTest, of the packages of the
trees tools/trees.json walks and of its layers' trees: its id and its labels.

Run from the checkout's root as python -I foundation/devkit/test/harness/list_tests.py
[--trees <tree>...] [--leave-out <name>...]: --trees, the trees it walks, by default
every one; --leave-out, tests it does not list, each by its id or a package's name,
which it still imports: discover loads every module before any is left out.
Each line is "<id>\t<labels by commas>", or "<name>\t<labels by
commas>\t<id>" for a test whose CTest name is not its id: the checks of
each module, from the table (checks(), below), rules.<M> for each module M
that holds a file of git's listing, run by structure's checks.py and
labelled fast, M and structure, and exemptions.<M> for each module that
holds a .clang-tidy or .ruleset file, run by build's checks.py and labelled
fast, M and build; rules.structure and exemptions.build carry their one
module's label. A
test's labels are its package's, by its whole name where PACKAGE_LABELS
names it so, or its module's where MODULE_LABELS names it, below, and those
its class names in ctest_labels, then the name of the
module of the table its file is in (the tree's module.json files, read by
foundation/devkit/python/table.py), by which the gate keys it
(tools/gate.py); the labels also give the watcher's limit for its tests
(ctest_tests.cmake, beside this file).

A module that does not import, a test the one reader of test code
(package(), below) says is no test code or puts in a package with no labels
here, a file in no module, a test directory with no
__init__.py, which discover would pass over, or a left-out name that matches
no test, fails the listing, and so the build.
"""

import argparse
import sys
import unittest
from pathlib import Path

# Under -I the checkout is off sys.path; appended, so that the standard library still wins.
sys.path.append(str(Path(__file__).resolve().parents[4]))
from foundation.devkit.python import git
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
    "foundation.devkit.build.tests.test_analysis": ("analysis",),
}
# The checks of each module: (CTest name's prefix, the module of the rules, the case id's prefix, which files
# give a module its test).
CHECKS = (
    ("rules.", "structure", "foundation.devkit.structure.checks.Check.test_", lambda rel: True),
    ("exemptions.", "build", "foundation.devkit.build.checks.Exemptions.test_",
     lambda rel: rel.endswith((".clang-tidy", ".ruleset"))),
)


def checks(root, table, kept):
    """[(name, labels, id)] of the checks of each module (CHECKS): a module's
    when it holds one of git's files, tracked or not ignored, that the
    working tree holds, under the kept trees where a project on top names
    them, each file under its first owning module."""
    files = [rel for rel in git.tracked(Path(root), others=True) if (Path(root) / rel).is_file()]
    if kept is not None:
        files = [rel for rel in files if rel.startswith(tuple(f"{tree}/" for tree in kept))]
    owner = module_table.owners(table, files)
    rows = []
    for prefix, rules_module, case, takes in CHECKS:
        held = sorted({names[0] for rel, names in owner.items() if names and takes(rel)})
        rows += [(prefix + name, tuple(dict.fromkeys(("fast", name, rules_module))), case + name) for name in held]
    return rows


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
    """([(name, labels, id)], [errors]) of the
    tests discover finds in each package of the kept trees, by default the
    trees tools/trees.json walks and its layers', from the checkout's root,
    each named by its id, and of the checks of each module (checks());
    but those left_out names, each by its id or a package's name, one that
    matches no test an error."""
    table = module_table.load(root)
    trees = table["trees"]
    named = checks(root, table, kept)
    kept = trees.every if kept is None else kept
    homes = {home.rpartition("/")[0] for home in table["homes"].values()}
    loader = unittest.TestLoader()
    suite = unittest.TestSuite(loader.discover(str(package), top_level_dir=str(root))
                               for package in packages(root, kept))
    rows, errors = [], [f"{d.relative_to(root).as_posix()}: a test directory with no __init__.py"
                        for d in unpackaged(root, kept)]
    unmatched = dict.fromkeys(left_out)
    for test in walk(suite):
        test_id = test.id()
        matched = [name for name in left_out if f"{test_id}.".startswith(f"{name}.")]
        if matched:
            for name in matched:
                unmatched.pop(name, None)
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
        owner = module_table.owners(table, [path])[path]
        if not owner:
            errors.append(f"{test_id}: its file is in no module of the table")
            continue
        rows.append((test_id, labels + (owner[0],), test_id))
    errors += [f"--leave-out {name}: no test of that id or package" for name in unmatched]
    return rows + named, errors + [str(error) for error in loader.errors]


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--trees", nargs="+")
    parser.add_argument("--leave-out", nargs="+", default=())
    args = parser.parse_args(argv)
    rows, errors = listed(kept=args.trees, left_out=args.leave_out)
    for name, labels, test_id in rows:
        print(f"{name}\t{','.join(labels)}" + (f"\t{test_id}" if test_id != name else ""))
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
