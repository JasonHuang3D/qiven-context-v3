"""The build's own Python tests: its globs and filters by trees.cmake and
tidy_config.cmake, and the standard library's hardened mode, run from the
root as

    python -I -m unittest discover -v -s foundation/devkit/build/tests -t . -k '*<test id>'

each test its own CTest test. A run whose selected tests all skipped
prints unittest's "OK (skipped=<n>)", which CTest's SKIP_REGULAR_EXPRESSION
reads as skipped (foundation/devkit/test/harness/ctest_tests.cmake); -v names
each skip's reason.
"""
