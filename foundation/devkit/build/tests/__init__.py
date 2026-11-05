"""The build's own Python tests: its globs and filters by trees.cmake and
tidy_config.cmake, every executable made by qiven_executable, and the
standard library's hardened mode, run from the root as

    python -I -m unittest discover -s foundation/devkit/build/tests -t . -k '*<test id>'

each test its own CTest test. As the test tree's (tests/__init__.py), a run
that skipped tests and passed and failed none ends with the skip code,
which CTest's SKIP_RETURN_CODE reports as skipped: the runner's Python half
(foundation/devkit/test/runner/skip.py) holds the code and the run's end.
"""

import os
import sys

from foundation.devkit.test.runner.skip import Run, exit_with_skip


def load_tests(loader, standard_tests, pattern):
    here = os.path.dirname(os.path.abspath(__file__))
    standard_tests.addTests(loader.discover(start_dir=here, pattern=pattern or "test*.py"))
    # unittest's main ends the run by sys.exit, which it looks up then.
    sys.exit = exit_with_skip
    return Run(standard_tests)
