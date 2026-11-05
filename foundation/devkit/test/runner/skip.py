"""The runner's Python half: a unittest run's skip, as the C++ runner's.

unittest ends a run whose tests were all skipped with 0; CTest reads a skip
from the exit status alone. So a run that skipped tests and passed and
failed none ends with the skip code, the C++ runner's exit_skip
(include/qiven/runner/core.hpp), which CTest's SKIP_RETURN_CODE reports as
skipped (ctest_tests.cmake). A package's load_tests wraps its suite in Run
and sets sys.exit to exit_with_skip, which unittest's main looks up when it
ends the run.
"""

import sys
import unittest

# The C++ runner's exit_skip.
SKIP_CODE = 125


class Run(unittest.TestSuite):
    """The whole discovered suite; it keeps the result it ran into."""

    result = None

    def run(self, result, debug=False):
        Run.result = result
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


def exit_with_skip(status=None):
    """sys.exit, but a run of skipped tests alone ends with SKIP_CODE."""
    if status in (None, 0) and _only_skipped(Run.result):
        for test, reason in Run.result.skipped:
            print(f"skipped {test.id()}: {reason}", file=sys.stderr)
        print(f"all selected tests skipped: exit status {SKIP_CODE}", file=sys.stderr)
        status = SKIP_CODE
    _exit(status)
