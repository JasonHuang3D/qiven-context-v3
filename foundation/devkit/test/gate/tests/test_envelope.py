"""envelope: the codes' order, the exit and the bound. A bound passed or a red
dropped from the answer hides a red; an ok exit on a red lands a commit on it."""

import json
import unittest

from foundation.devkit.test.gate import envelope


def red(number, preset="ci-windows", modules=3):
    return {"test": f"m{number % modules}.Case{number}.{'x' * 60}", "outcome": "failed", "ran": False,
            "since": "run:20261107T100000Z-aaaa", "preset": preset, "flips": 0, "log": "L" * 100,
            "modules": [f"m{number % modules}"]}


class Codes(unittest.TestCase):
    def test_order_and_exit(self):
        self.assertEqual(envelope.worst(["PASSED", "RED_KEPT", "NOTHING_TO_RUN"]), "RED_KEPT")
        self.assertEqual(envelope.worst(["PLANNED", "TESTS_FAILED"]), "TESTS_FAILED")
        self.assertEqual(envelope.worst(["NOTHING_TO_RUN", "PLANNED"]), "PLANNED")
        self.assertEqual(envelope.worst(["BUILD_FAILED", "INTERNAL"]), "INTERNAL")
        self.assertEqual([envelope.exit_status(c) for c in ("PASSED", "NOTHING_TO_RUN", "PLANNED", "RED_KEPT")],
                         [0, 0, 1, 1])
        self.assertIsNone(envelope.action("PASSED", "x"))
        self.assertEqual(envelope.action("RED_KEPT", "y"), "fix: y")

    def test_owner_lists_the_red(self):
        """A red's next action names a gate whose step lists it: the release
        tree's red outside the debugger's label goes to presets, not to a gate
        whose ask would find no test."""
        owners = {"release": [("debugger", "^debugger$", None), ("presets", None, None)]}
        mine = {**red(1, "release"), "labels": ["python", "m1"]}
        cdb = {**red(2, "release"), "labels": ["debugger", "m2"]}
        built = {"build": "FAILED: x", "preset": "release", "since": "run:R", "log": "L", "modules": []}
        self.assertEqual([envelope.owner(r, owners) for r in (mine, cdb, built, red(3, "release"))],
                         ["presets", "debugger", "debugger", "presets"])
        long = {**mine, "log": "L" * 260}  # the clip at 300 code points cuts the words, never the ask
        self.assertIn(f"tools/gate.py presets --again test:{mine['test']},", envelope.red_action(long, owners))


class Bound(unittest.TestCase):
    def check(self, code, bound, reds, lists):
        line = envelope.bounded_line(code, "fix: " + "n" * 400, "run:R", "D", {"reason": "r" * 400}, reds, lists)
        self.assertLessEqual(len(line.encode("utf-8")), bound)
        answer = json.loads(line)
        data = answer["data"]
        self.assertLessEqual(len(answer["next_action"]), 300)
        self.assertLessEqual(len(data["red"]), envelope.RED_FULL)
        counted = sum(by if isinstance(by, int) else sum(by.values()) for by in data["red_more"].values())
        self.assertEqual(len(data["red"]) + counted, len(reds))  # no red uncounted
        for name in envelope.LISTS:
            self.assertLessEqual(len(data[name]), envelope.LIST_MOST)
            self.assertEqual(len(data[name]) + data["more"].get(name, 0), len(lists.get(name, ())))
        return data

    def test_reds_first(self):
        """Many reds and long lists: within 8,192 bytes, 20 reds in full, every
        further one counted by configuration and module, the lists cut."""
        reds = [red(n) for n in range(60)] + [red(n, "asan") for n in range(40)]
        lists = {name: [f"{name} {n} " + "e" * 280 for n in range(50)] for name in envelope.LISTS}
        data = self.check("RED_KEPT", envelope.BOUND, reds, lists)
        self.assertEqual(len(data["red"]), 20)
        self.assertEqual(data["red_more"]["asan"], {"m0": 14, "m1": 13, "m2": 13})
        self.assertNotIn("modules", data["red"][0])
        cut = [name for name in envelope.LISTS if data["more"].get(name)]
        first = next(i for i, name in enumerate(envelope.LISTS) if data["more"].get(name, 0) > 30)
        self.assertTrue(all(not data[name] for name in envelope.LISTS[first + 1:]), cut)

    def test_reds_by_configuration(self):
        """G-4 replayed: 600 reds over 3 configurations of 200 modules each,
        their counts by module past the room: counted by configuration, the
        first 20 in full, data never cut to its reason."""
        reds = [red(n, preset, modules=200) for preset in ("ci-windows", "alone", "asan") for n in range(200)]
        data = self.check("TESTS_FAILED", envelope.BOUND, reds, {})
        self.assertEqual((len(data["red"]), data["red_more"]), (20, {"ci-windows": 180, "alone": 200, "asan": 200}))

    def test_lists_at_twenty(self):
        """Short lists: each at most 20 entries, the rest in more; a list cut
        at 20 stops no list after it."""
        lists = {"faults": [f"f{n}" for n in range(25)], "gone": ["g"]}
        data = self.check("PASSED", envelope.BOUND, [], lists)
        self.assertEqual((len(data["faults"]), data["more"], data["gone"]), (20, {"faults": 5}, ["g"]))

    def test_refusal(self):
        """A refusal within 4,096 bytes."""
        self.check("TOOL_MISSING", envelope.REFUSAL_BOUND, [red(n) for n in range(30)],
                   {"faults": ["f" * 290] * 30})
