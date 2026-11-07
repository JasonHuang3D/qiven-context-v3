"""Python's case entry: behind the watcher, in front of each Python test CTest
runs, it runs the one test its id names and gives the outcome by its exit,
as the C++ runner's cases do.

    python -I foundation/devkit/test/harness/case.py --skip <code> <test id>

- From the checkout's root, CTest's working directory for it, it appends
  the root, four directories above this file, to sys.path, so that the
  standard library still wins, and loads the test by its id, the listing's
  (list_tests.py), by unittest's loader by name; a name that cannot be
  loaded runs as a failed test.
- unittest's lines go to standard error, each test's with its skip's
  reason (verbosity 2).
- Its exit: 1 for a failed run; else <code>, the suite's skip code
  (cases.cmake's QIVEN_SKIP_CODE, 125), where it skipped, each skip first
  named on a line "SKIP <test id>: <reason>", as the C++ runner names its
  own; else 1 where no test ran, named "no test ran: <test id>"; else 0.
  A test that skipped a subtest and passed the rest is a skip.
- The watcher's status line follows its lines, last.

Its standard library alone, so that a copy of it runs in a scratch checkout.
"""

import argparse
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
FAILED = 1


def parse(argv):
    parser = argparse.ArgumentParser(prog="case.py", description="Runs one Python test and names its outcome by its exit.")
    parser.add_argument("--skip", type=int, required=True, help="the exit code of a skip, the suite's")
    parser.add_argument("test", help="the test's id, its dotted name from the root")
    return parser.parse_args(argv)


def outcome(result, test, skip):
    """The exit code for the run's result, a skip's or no test's line printed."""
    if not result.wasSuccessful():
        return FAILED
    if result.skipped:
        for case, reason in result.skipped:
            print(f"SKIP {case.id()}: {reason}", file=sys.stderr, flush=True)
        return skip
    if result.testsRun == 0:
        print(f"no test ran: {test}", file=sys.stderr, flush=True)
        return FAILED
    return 0


def main(argv=None):
    args = parse(sys.argv[1:] if argv is None else argv)
    root = str(ROOT)
    if root not in sys.path:
        sys.path.append(root)  # under -I the checkout is off sys.path; appended, so that the standard library still wins
    suite = unittest.defaultTestLoader.loadTestsFromName(args.test)
    result = unittest.TextTestRunner(stream=sys.stderr, verbosity=2).run(suite)
    return outcome(result, args.test, args.skip)


if __name__ == "__main__":
    sys.exit(main())
