"""The tooling's own Python tests: tools/debug.py and the modules' check over
the tree, run from the root as

    python -I -m unittest discover -s tools -t . -k '*<test id>'

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
