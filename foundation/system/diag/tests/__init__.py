"""Diag's own Python tests: every executable made by qiven_executable and
every binary of the build running the entry, run from the root as

    python -I foundation/devkit/test/harness/case.py --skip 125 '<test id>'

each test its own CTest test, run by harness' case entry
(foundation/devkit/test/harness/case.py), 125 a skip, its reason printed.
"""
