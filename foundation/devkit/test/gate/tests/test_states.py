"""states: the design's table of the states, row by row. A transition wrong
in silence carries a pass or drops a red: a red reported green, or a flake read
as a fix."""

import unittest

from foundation.devkit.test.gate import junit, states

KEY, TREE, CODE = "k1", "t1", "c1"
EARLIER, NOW = "20261107T100000Z-aaaa", "20261107T110000Z-bbbb"
PASSED, SKIPPED = junit.Ran("passed", 1.5, ""), junit.Ran("skipped", 0.1, "SKIP_RETURN_CODE=125")
FAILED, TIMEOUT = junit.Ran("failed", 2.0, "status: 1 failed"), junit.Ran("timeout", 60.0, "Timeout")
NOT_STARTED = junit.Ran(junit.NOT_STARTED, None, "no testcase in the JUnit")


def record(outcome, flaky=False, flips=0, key=KEY, tree=TREE, code=CODE):
    return {"key": key, "tree": tree, "code": code, "outcome": outcome, "flaky": flaky, "since": EARLIER,
            "flips": flips, "seconds": 1.0}


def after(old, ran, code=CODE, key=KEY, tree=TREE):
    return states.after(old, key, tree, code, NOW, ran)


class Rows(unittest.TestCase):
    def check(self, new, outcome, flaky, since, flips):
        self.assertEqual((new["outcome"], new["flaky"], new["since"], new["flips"]), (outcome, flaky, since, flips))

    def test_none(self):
        """No record, or one at another key: selected new or changed; the
        outcome, since this run, flips kept, 0 where new."""
        self.assertEqual(states.reason(None, KEY, TREE), "new")
        self.check(after(None, PASSED), "passed", False, NOW, 0)
        self.check(after(None, FAILED), "failed", False, NOW, 0)
        other = record("failed", flaky=True, flips=3, key="k0")
        self.assertEqual(states.reason(other, KEY, TREE), "changed")
        self.check(after(other, PASSED), "passed", False, NOW, 3)
        self.check(after(other, TIMEOUT), "timeout", False, NOW, 3)
        new = after(other, PASSED, code="c9")
        self.assertEqual((new["key"], new["tree"], new["code"], new["seconds"]), (KEY, TREE, "c9", 1.5))

    def test_green(self):
        """Green at this key and tree: left out; then green keeps since; then
        red, since this run, flips + 1 at the same code id alone."""
        old = record("passed", flips=2)
        self.assertIsNone(states.reason(old, KEY, TREE))
        self.check(after(old, SKIPPED), "skipped", False, EARLIER, 2)
        self.check(after(old, FAILED), "failed", False, NOW, 3)
        self.check(after(old, FAILED, code="c2"), "failed", False, NOW, 2)

    def test_red(self):
        """Red: left out, and red; then green at the same code id is flaky,
        since kept, flips + 1; at another a fix, green since this run; then
        red keeps since."""
        old = record("failed", flips=1)
        self.assertIsNone(states.reason(old, KEY, TREE))
        self.assertTrue(states.is_red(old))
        flake = after(old, PASSED)
        self.check(flake, "passed", True, EARLIER, 2)
        self.assertEqual((states.state(flake), states.colour(flake)), ("flaky", "red"))
        fixed = after(old, PASSED, code="c2")
        self.check(fixed, "passed", False, NOW, 1)
        self.assertEqual(states.colour(fixed), "green")
        self.check(after(old, TIMEOUT), "timeout", False, EARLIER, 1)

    def test_flaky(self):
        """Flaky: as red; then green at the same code id stays flaky; at
        another the flake ended, green since this run; then red, since kept,
        flips + 1 at the same code id alone."""
        old = record("passed", flaky=True, flips=2)
        self.assertIsNone(states.reason(old, KEY, TREE))
        self.assertTrue(states.is_red(old))
        self.check(after(old, PASSED), "passed", True, EARLIER, 2)
        self.check(after(old, PASSED, code="c2"), "passed", False, NOW, 2)
        self.check(after(old, FAILED), "failed", False, EARLIER, 3)
        self.check(after(old, FAILED, code="c2"), "failed", False, EARLIER, 2)

    def test_another_tree(self):
        """At this key and another tree: selected rebuilt, then as its state's row."""
        for old, ran, row in ((record("passed"), FAILED, ("failed", False, NOW, 1)),
                              (record("failed"), PASSED, ("passed", True, EARLIER, 1)),
                              (record("passed", flaky=True), PASSED, ("passed", True, EARLIER, 0))):
            with self.subTest(state=states.state(old), ran=ran.outcome):
                self.assertEqual(states.reason(old, KEY, "t2"), "rebuilt")
                new = after(old, ran, tree="t2")
                self.check(new, *row)
                self.assertEqual(new["tree"], "t2")

    def test_no_transition(self):
        """A NOT_STARTED makes no transition: the record as it was."""
        old = record("failed", flips=1)
        self.assertIs(after(old, NOT_STARTED, code="c2"), old)
        self.assertIsNone(after(None, NOT_STARTED))

    def test_gone(self):
        """A record the listing names no more is gone."""
        self.assertEqual(states.gone({"a": 1, "b": 2, "c": 3}, ["b"]), ["a", "c"])

    def test_build(self):
        """A failed build is its configuration's red, since its first failed
        run, until its tree builds."""
        state = {"build": None}
        states.build_failed(state, EARLIER, ["build: failed"])
        states.build_failed(state, NOW, ["build: failed again"])
        self.assertEqual(state["build"], {"since": EARLIER, "run": NOW, "lines": ["build: failed again"]})
        states.build_passed(state)
        self.assertIsNone(state["build"])
