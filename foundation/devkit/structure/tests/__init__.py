"""The structure method's own tests: each rule held to the tree and to its
planted findings, run from the root as

    python -I -m unittest discover -s foundation/devkit/structure -t . -k '*<test id>'

each test its own CTest test. As the test tree's (tests/__init__.py), a run
that skipped tests and passed and failed none ends with the suite's skip
code, which CTest's SKIP_RETURN_CODE reports as skipped.
"""

import os
import sys

import tests


def load_tests(loader, standard_tests, pattern):
    here = os.path.dirname(os.path.abspath(__file__))
    standard_tests.addTests(loader.discover(start_dir=here, pattern=pattern or "test*.py"))
    # unittest's main ends the run by sys.exit, which it looks up then.
    sys.exit = tests._exit_with_skip
    return tests._Run(standard_tests)
