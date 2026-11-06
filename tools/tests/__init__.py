"""The tooling's own Python tests: tools/debug.py and the modules' check over
the tree, run from the root as

    python -I -m unittest discover -v -s tools -t . -k '*<test id>'

each test its own CTest test. A run whose selected tests all skipped
prints unittest's "OK (skipped=<n>)", which CTest's SKIP_REGULAR_EXPRESSION
reads as skipped (foundation/devkit/test/runner/ctest_tests.cmake); -v names
each skip's reason.
"""
