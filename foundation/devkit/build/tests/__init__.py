"""The build's own Python tests: its globs and filters by trees.cmake and
tidy_config.cmake, and the standard library's hardened mode, run from the
root as

    python -I foundation/devkit/test/harness/case.py --skip 125 '<test id>'

each test its own CTest test, run by harness' case entry
(foundation/devkit/test/harness/case.py), 125 a skip, its reason printed.
"""
