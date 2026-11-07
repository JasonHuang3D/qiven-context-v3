"""Time's own Python tests: utc's cases run as processes under each zone
TZ names, run from the root as

    python -I foundation/devkit/test/harness/case.py --skip 125 '<test id>'

each test its own CTest test, run by harness' case entry
(foundation/devkit/test/harness/case.py), 125 a skip, its reason printed.
"""
