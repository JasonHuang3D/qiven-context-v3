"""The Python tests of qiven3, run from the root as

    python -I -m unittest discover -s tests -t . -k '*<test id>'

each test its own CTest test. unittest ends a run whose tests were all
skipped with 0; CTest reads a skip from the exit status alone. So a run
that skipped tests and passed and failed none ends with the suite's skip
code, the C++ runner's, which CTest's SKIP_RETURN_CODE reports as skipped.
"""

import os
import sys
import unittest

# The C++ runner's exit_skip.
SKIP_CODE = 125


class _Run(unittest.TestSuite):
    """The whole discovered suite; it keeps the result it ran into."""

    result = None

    def run(self, result, debug=False):
        _Run.result = result
        return super().run(result, debug)


def _only_skipped(result):
    if result is None or not result.wasSuccessful() or not result.skipped:
        return False
    if result.expectedFailures:
        return False
    # A skipped test is counted in testsRun; a class skipped in setUpClass
    # is in skipped alone.
    return result.testsRun <= len(result.skipped)


_exit = sys.exit


def _exit_with_skip(status=None):
    if status in (None, 0) and _only_skipped(_Run.result):
        print(f"all selected tests skipped: exit status {SKIP_CODE}", file=sys.stderr)
        status = SKIP_CODE
    _exit(status)


def load_tests(loader, standard_tests, pattern):
    here = os.path.dirname(os.path.abspath(__file__))
    standard_tests.addTests(loader.discover(start_dir=here, pattern=pattern or "test*.py"))
    # unittest's main ends the run by sys.exit, which it looks up then.
    sys.exit = _exit_with_skip
    return _Run(standard_tests)
