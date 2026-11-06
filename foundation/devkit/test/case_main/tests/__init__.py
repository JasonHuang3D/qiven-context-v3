"""The case main's own Python test: the routes of a case binary, its main
and the runner, through a process, run from the root as

    python -I -m unittest discover -v -s foundation/devkit/test/case_main/tests -t . -k '*<test id>'

each test its own CTest test. A run whose selected tests all skipped
prints unittest's "OK (skipped=<n>)", which CTest's SKIP_REGULAR_EXPRESSION
reads as skipped (foundation/devkit/test/runner/ctest_tests.cmake); -v names
each skip's reason.
"""
