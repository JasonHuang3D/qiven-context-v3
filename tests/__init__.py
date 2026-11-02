"""The Python tests of qiven3, run from the root as

    python -I -m unittest discover -s tests -t . -k '*<test id>'

each test its own CTest test. unittest ends a run whose tests were all
skipped with 0; CTest reads a skip from the exit status alone. So a run
that skipped tests and passed and failed none ends with the suite's skip
code, the C++ runner's, which CTest's SKIP_RETURN_CODE reports as skipped:
the runner's Python half (foundation/devkit/test/runner/skip.py) holds the
code and the run's end, which this package and its drives import.
"""

import os
import sys

from foundation.devkit.test.runner.skip import SKIP_CODE, Run, exit_with_skip

__all__ = ("SKIP_CODE", "load_tests")


def load_tests(loader, standard_tests, pattern):
    here = os.path.dirname(os.path.abspath(__file__))
    standard_tests.addTests(loader.discover(start_dir=here, pattern=pattern or "test*.py"))
    # unittest's main ends the run by sys.exit, which it looks up then.
    sys.exit = exit_with_skip
    return Run(standard_tests)
