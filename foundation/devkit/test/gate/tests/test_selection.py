"""selection: who runs, and the plan's time. A selection wrong in silence
leaves a changed test unrun, read as green at its key, or drops an ask."""

import unittest

from foundation.devkit.test.gate import selection

TREE = "t1"


def record(outcome="passed", key="k", tree=TREE, flaky=False, seconds=1.0):
    return {"key": key, "tree": tree, "code": "c", "outcome": outcome, "flaky": flaky, "since": "run:R0",
            "flips": 0, "seconds": seconds}


LISTED = {"new": ["a"], "changed": ["a"], "rebuilt": ["a"], "green": ["a"], "red": ["b"], "flaky": ["b"]}
RECORDS = {"changed": record(key="old"), "rebuilt": record(tree="t0"), "green": record(),
           "red": record("failed"), "flaky": record(flaky=True)}
KEYS = dict.fromkeys(LISTED, "k")


class Choose(unittest.TestCase):
    def test_by_key(self):
        """No record, another key or another tree selects; the rest is left
        out with its colour and the run it stands on."""
        chosen = selection.choose(LISTED, RECORDS, KEYS, TREE)
        self.assertEqual(chosen.selected, {"new": "new", "changed": "changed", "rebuilt": "rebuilt"})
        self.assertEqual(chosen.left_out, {"green": {"colour": "green", "since": "run:R0"},
                                           "red": {"colour": "red", "since": "run:R0"},
                                           "flaky": {"colour": "red", "since": "run:R0"}})
        self.assertEqual(chosen.unmatched, [])

    def test_asks(self):
        """An ask adds and narrows nothing; red asks each red and flake; a
        name no test carries is unmatched; a step's all asks every test."""
        asks = [selection.ask(t) for t in ("test:green", "module:b", "test:none", "module:z", "red")]
        chosen = selection.choose(LISTED, RECORDS, KEYS, TREE, asks)
        self.assertEqual(chosen.selected["green"], "asked")
        self.assertEqual((chosen.selected["red"], chosen.selected["flaky"], chosen.selected["new"]),
                         ("asked", "asked", "new"))
        self.assertEqual(chosen.unmatched, ["test:none", "module:z"])
        self.assertEqual(selection.choose(LISTED, {"green": record()}, KEYS, TREE, [selection.ask("red")]).unmatched,
                         [])
        every = selection.choose(LISTED, RECORDS, KEYS, TREE, every=True)
        self.assertEqual((len(every.selected), every.left_out), (6, {}))

    def test_ask_forms(self):
        for wrong in ("tests:a", "test:", "module", "reds", ""):
            with self.subTest(wrong=wrong), self.assertRaises(ValueError):
                selection.ask(wrong)
        self.assertEqual(selection.spelled(selection.ask("module:a")), "module:a")


class Plan(unittest.TestCase):
    def test_expected(self):
        """The larger of the sum over the jobs and the longest; a test with no
        duration counted apart."""
        records = {"a": record(seconds=10.0), "b": record(seconds=2.0), "c": record(seconds=None)}
        self.assertEqual(selection.plan(["a", "b", "c", "d"], records, 2), selection.Plan(4, 10.0, 2))
        records = {n: record(seconds=3.0) for n in "abcd"}
        self.assertEqual(selection.plan(list("abcd"), records, 2), selection.Plan(4, 6.0, 0))
        self.assertEqual(selection.plan([], records, 12), selection.Plan(0, 0.0, 0))
