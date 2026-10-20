"""Print each Python test of tests/ for CTest: its id, its labels, serial.

Run from the checkout's root as python -I tests/runner/list_tests.py. Each
line is "<id>\t<labels by commas>\t<0 or 1>". A test's labels are its
package's, or its module's where MODULE_LABELS names it, below, and those
its class names in ctest_labels; a class whose ctest_serial is true runs
alone. A module that does not import, or a test in a package with no labels
here, fails the listing, and so the build.
"""

import sys
import unittest

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


def main():
    loader = unittest.TestLoader()
    suite = loader.discover("tests", top_level_dir=".")
    failed = False
    for test in walk(suite):
        test_id = test.id()
        error = getattr(test, "_exception", None)
        if error is not None or not test_id.startswith("tests."):
            print(f"{test_id}: {error}", file=sys.stderr)
            failed = True
            continue
        package = test_id.split(".")[1]
        if package not in PACKAGE_LABELS:
            print(f"{test_id}: the package {package} has no labels", file=sys.stderr)
            failed = True
            continue
        module = ".".join(test_id.split(".")[1:3])
        labels = MODULE_LABELS.get(module, PACKAGE_LABELS[package]) + tuple(getattr(test, "ctest_labels", ()))
        serial = 1 if getattr(test, "ctest_serial", False) else 0
        print(f"{test_id}\t{','.join(labels)}\t{serial}")
    for error in loader.errors:
        print(error, file=sys.stderr)
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
