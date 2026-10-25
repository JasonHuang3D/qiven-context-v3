"""Tier 1's reach: every test this build registers carries a label one of
tier 1's ctest steps selects, or a label the label table runs outside
tier 1, or is the one case kept out by name (BY_NAME), so that a case
cannot fall out of the edit loop unseen. Tier 1's
selection is read from the loop's own table: each ctest step of each
system's tier 1 job and the steps tier 1 adds, by its test preset's label
filter or its own -L and -LE. Name filters stay out of the reach: the
structure tests Windows leaves out by name run in tier 2's own steps."""

import json
import re
import unittest

from tests.drive import binaries
from tests.drive.test_loop_ci import load_loop
from tests.drive.test_wiring import LABELS, ctest_tests

# The labels the table runs outside tier 1: the loop's debugger cases, the
# full suite's slow, machine, oracle and gpu cases, and the fuzz, bench,
# drill and analysis presets'.
KEPT_OUT = {"debugger", "slow", "machine", "oracle", "gpu", "fuzz", "bench", "drill", "analysis"}
# The one case kept out by name, for which the table names no label yet:
# H6, on the nohal build tier 3's suite makes beside this one; tier 1 builds
# none, and would read an earlier run's.
BY_NAME = {"tests.contract.test_hal_build.NoHal.test_H6_core_without_it"}


def tier_one_filters(loop, presets):
    """Each ctest step tier 1 runs, on either system: (job, step, include,
    exclude), each a label regex or None."""
    table, found = loop.TABLE, []
    for job in table["TIER1"].values():
        for step in [*loop.JOBS[job][2], *table["TIER1_STEPS"].get(job, ())]:
            args = list(step.ci)
            if args[0] != "ctest":
                continue
            include = exclude = None
            if "--preset" in args:
                chosen = presets[args[args.index("--preset") + 1]].get("filter", {})
                include = chosen.get("include", {}).get("label")
                exclude = chosen.get("exclude", {}).get("label")
            if "-L" in args:
                include = args[args.index("-L") + 1]
            if "-LE" in args:
                exclude = args[args.index("-LE") + 1]
            found.append((job, step.name, include, exclude))
    return found


def reached(filters, labels):
    """Whether one of the steps selects a test of these labels, as ctest
    matches a label regex: by search, against each label."""
    def hit(pattern):
        return any(re.search(pattern, label) for label in labels)
    return any((include is None or hit(include)) and not (exclude and hit(exclude))
               for _, _, include, exclude in filters)


class Reach(unittest.TestCase):
    ctest_labels = ("fake",)
    ctest_reads = ("tests/",)

    def test_every_test_reached_or_kept_out(self):
        presets = json.loads((binaries.ROOT / "CMakePresets.json").read_bytes())
        filters = tier_one_filters(load_loop(), {p["name"]: p for p in presets["testPresets"]})
        # Tier 1 runs the fake and fast selections on both systems, and no
        # label kept out of it.
        self.assertTrue(reached(filters, {"fast"}) and reached(filters, {"fake"}), filters)
        self.assertLessEqual(KEPT_OUT, LABELS)
        self.assertEqual([label for label in sorted(KEPT_OUT) if reached(filters, {label})], [], filters)
        tests = ctest_tests()
        self.assertTrue(tests)
        self.assertLessEqual(BY_NAME, set(tests))
        missing = sorted(f"{name} [{','.join(props.get('LABELS', []))}]" for name, (_, props) in tests.items()
                         if name not in BY_NAME and not reached(filters, set(props.get("LABELS", [])))
                         and not KEPT_OUT & set(props.get("LABELS", [])))
        if missing:
            self.fail(f"{len(missing)} tests tier 1 cannot select, with no label the table keeps out of it:\n"
                      + "\n".join(missing))
