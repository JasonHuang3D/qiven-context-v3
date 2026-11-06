"""The Python tests of qiven3, run from the root as

    python -I -m unittest discover -s tests -t . -k '*<test id>'

each test its own CTest test. unittest ends a run whose tests were all
skipped with 0; CTest reads a skip from the exit status alone. So a run
that skipped tests and passed and failed none ends with the suite's skip
code, the C++ runner's, which CTest's SKIP_RETURN_CODE reports as skipped:
the runner's Python half (foundation/devkit/test/runner/skip.py) holds the
code and the run's end.
"""

from foundation.devkit.test.runner import skip

load_tests = skip.loader(__file__)
