"""Log's own Python tests: K13's halves over the build's binary and its
planted field, run from the root as

    python -I -m unittest discover -s foundation/services/log/tests -t . -k '*<test id>'

each test its own CTest test. As the test tree's (tests/__init__.py), a run
that skipped tests and passed and failed none ends with the skip code,
which CTest's SKIP_RETURN_CODE reports as skipped: the runner's Python half
(foundation/devkit/test/runner/skip.py) holds the code and the run's end.
"""

from foundation.devkit.test.runner import skip

load_tests = skip.loader(__file__)
