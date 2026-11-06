"""Compute's own Python tests: the process a caller's mistake on its fake
ends, run from the root as

    python -I -m unittest discover -v -s foundation/services/compute/tests -t . -k '*<test id>'

each test its own CTest test. A run whose selected tests all skipped
prints unittest's "OK (skipped=<n>)", which CTest's SKIP_REGULAR_EXPRESSION
reads as skipped (foundation/devkit/test/runner/ctest_tests.cmake); -v names
each skip's reason.
"""
