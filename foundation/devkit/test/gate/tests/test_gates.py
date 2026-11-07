"""gates: on planted gates files and a planted store in a scratch directory,
load's checks, steps_of's steps with their build presets' targets,
owners_of's owners and the gate a red's ask names, the reach's matcher, and
out_of_reach, a step its reach leaves out run where an ask names a test it
lists (G-5) and where its configuration holds a kept build red (H-1). Wrong
in silence, a gate skips a step it owes, or a red's ask sends its fix to a
gate that never ends it."""

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from foundation.devkit.test.gate import envelope, gates, key, run, selection, store

COMMAND = envelope.COMMAND
RUN = "run:20261107T000000Z-0000"
PATTERN = r"^tools[.]tests[.]test_debug[.]"
LISTED = "tools.tests.test_debug.Debug.test_k16"  # a test the release step lists, labels its own
UNLISTED = "foundation.devkit.python.tests.test_answer.Answer.test_bound"  # a release test it does not
REACH = {"paths": ["tools/debug.py", "foundation/devkit/test/harness/"]}
STEP = run.Step("release", pattern=PATTERN)
BLOB, OTHER = "a" * 40, "b" * 40

# The commit gate's form, planted: the daily tree, then release by reach; release and the daily tree its own.
PLANTED = {
    "format": 1, "toolset": "14.44.35207",
    "owners": {"ci-windows": "commit", "release": "commit", "asan": "asan"},
    "gates": {
        "commit": {"steps": [{"preset": "ci-windows", "select": "changed"},
                             {"preset": "release", "select": "changed", "pattern": PATTERN, "reach": REACH}]},
        "debugger": {"steps": [{"preset": "release", "select": "changed", "labels": "^debugger$", "repeat": 6}]},
        "asan": {"steps": [{"preset": "asan", "select": "changed", "labels": "^cxx$"}]},
        "presets": {"steps": [{"preset": "release", "select": "all"}, {"preset": "ci-windows", "select": "all"},
                              {"preset": "asan", "select": "all", "labels": "^cxx$"}]},
    },
}


def planted(**parts):
    """PLANTED with its top-level parts replaced by parts, a copy."""
    return {**json.loads(json.dumps(PLANTED)), **parts}


def record(labels, outcome="passed"):
    return {"labels": labels, "outcome": outcome, "flaky": False, "flips": 0, "since": RUN, "key": "k"}


class Scratch(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="qiven-gate-gates-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.file = self.root / "gates.json"

    def put(self, data):
        self.file.write_text(data if isinstance(data, str) else json.dumps(data), encoding="utf-8")
        return self.file


class Load(Scratch):
    def refused(self, data, words):
        with self.assertRaises(gates.BadGates) as caught:
            gates.load(self.put(data))
        self.assertIn(words, str(caught.exception))

    def test_loads(self):
        """A file in its form loads as it stands."""
        self.assertEqual(gates.load(self.put(PLANTED)), PLANTED)

    def test_refused(self):
        """Each form load refuses, by one line naming where."""
        first = {"preset": "p", "select": "all"}

        def g(*steps):
            return planted(owners={}, gates={"g": {"steps": list(steps)}})
        self.refused("{", "gates.json")
        self.refused(planted(format=2), "not format 1")
        self.refused(planted(owners=[]), "no gates or no owners")
        self.refused(planted(owners={"release": "nowhere"}), "owners.release names no gate: nowhere")
        self.refused(g(), "g has no steps")
        self.refused(g({"preset": "p", "select": "some"}), "g.steps[0]: select")
        self.refused(g({**first, "odd": 1}), "g.steps[0]: a step holds")
        self.refused(g({**first, "labels": 1}), "g.steps[0].labels")
        self.refused(g({**first, "repeat": 0}), "g.steps[0].repeat")
        self.refused(g({**first, "reach": REACH}), "first step has no reach")
        second = {"preset": "q", "select": "changed", "reach": {"trees": ["layers"]}}
        self.refused(g(first, second), "trees and suffixes go together")
        second["reach"] = {"trees": ["nothing"], "suffixes": [".cpp"]}
        self.refused(g(first, second), "no list of trees.json: nothing")
        second["reach"] = {"paths": []}
        self.refused(g(first, second), "g.steps[1].reach.paths")


class Steps(Scratch):
    def test_steps(self):
        """Each step a run.Step with its reach, its targets its build preset's where it names them."""
        presets = {"buildPresets": [{"name": "ci-windows", "configurePreset": "ci-windows", "targets": "all_ci"}]}
        (self.root / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
        steps = gates.steps_of(PLANTED["gates"]["commit"], self.root)
        self.assertEqual(steps, [(run.Step("ci-windows", ".", False, None, None, None, ["all_ci"]), None),
                                 (run.Step("release", ".", False, None, PATTERN, None, None), REACH)])
        (one, reach), = gates.steps_of(PLANTED["gates"]["debugger"], self.root)
        self.assertEqual((one.labels, one.repeat, one.every, reach), ("^debugger$", 6, False, None))
        self.assertTrue(gates.steps_of(PLANTED["gates"]["presets"], self.root)[0][0].every)


class Owners(unittest.TestCase):
    def test_release_red_asks_commit(self):
        """G-5: a release red its commit step lists names commit, a debugger
        label or none; one it does not list falls to presets; a daily red names
        commit, an asan red its one-tree gate; a release build's red names
        commit, with no ask, its release step running and building while that
        red is kept, whatever its reach (H-1, Reach.test_build_red)."""
        owners = gates.owners_of(PLANTED)
        self.assertEqual(owners["release"], [("commit", None, PATTERN), ("presets", None, None)])
        for labels in (["py", "tooling"], ["debugger", "tooling"]):
            red = {"test": LISTED, "preset": "release", "labels": labels}
            self.assertEqual(envelope.ask(red, owners), f"{COMMAND} commit --again test:{LISTED}")
        red = {"test": UNLISTED, "preset": "release", "labels": ["python"]}
        self.assertEqual(envelope.ask(red, owners), f"{COMMAND} presets --again test:{UNLISTED}")
        red = {"test": UNLISTED, "preset": "ci-windows", "labels": ["python"]}
        self.assertEqual(envelope.ask(red, owners), f"{COMMAND} commit --again test:{UNLISTED}")
        red = {"test": "a.b", "preset": "asan", "labels": ["cxx"]}
        self.assertEqual(envelope.ask(red, owners), f"{COMMAND} asan --again test:a.b")
        self.assertEqual(envelope.ask({"build": "error", "preset": "release"}, owners), f"{COMMAND} commit")


class Matcher(unittest.TestCase):
    def test_paths_and_trees(self):
        """A path held by name, under a path ending in /, or under a tree with a suffix."""
        trees = SimpleNamespace(layers=("foundation", "src"), analysed="tests/analysed",
                                source_suffixes=(".cpp",), header_suffixes=(".hpp", ".h"))
        held = gates.matcher(REACH, trees)
        self.assertTrue(held("tools/debug.py"))
        self.assertTrue(held("foundation/devkit/test/harness/watch.py"))
        self.assertFalse(held("tools/debug.pyc"))
        self.assertFalse(held("tools/gate.py"))
        held = gates.matcher({"trees": ["layers", "analysed"], "suffixes": ["source_suffixes", ".ipp"]}, trees)
        self.assertTrue(held("src/a/b.cpp"))
        self.assertTrue(held("tests/analysed/c.ipp"))
        self.assertFalse(held("src/a/b.hpp"))
        self.assertFalse(held("tools/b.cpp"))


class Reach(Scratch):
    def plant(self):
        place = store.place(self.root, STEP.preset)
        place.mkdir(parents=True)
        state = store.empty(STEP.preset)
        state.update(run=RUN, unowned={"tools/debug.py": BLOB, "tools/gate.py": BLOB},
                     tests={LISTED: record(["debugger", "tooling"]),
                            UNLISTED: record(["python"], outcome="failed")})
        store.write(place, state)
        return place

    def why(self, *asks, debug=BLOB, other=BLOB):
        files = key.Files({}, {"tools/debug.py": debug, "tools/gate.py": other})
        return gates.out_of_reach(self.root, STEP, REACH, files, None, [selection.ask(a) for a in asks])

    def test_files(self):
        """No store runs the step; no file of its reach changed leaves it out,
        a change outside its reach too; a change in it runs it."""
        self.assertIsNone(self.why())
        self.plant()
        self.assertEqual(self.why(), f"left out by its reach: no file of it changed since {RUN}")
        self.assertIsNotNone(self.why(other=OTHER))
        self.assertIsNone(self.why(debug=OTHER))

    def test_unreadable(self):
        """An unreadable store runs the step, whose own answer names it."""
        place = store.place(self.root, STEP.preset)
        place.mkdir(parents=True)
        (place / "state.json").write_text("{", encoding="utf-8")
        self.assertIsNone(self.why())

    def test_asks(self):
        """G-5: an ask of a test the step lists runs it, reach aside, by
        name, module or red; an ask of none leaves it out, and says so."""
        place = self.plant()
        self.assertIsNone(self.why(f"test:{LISTED}"))
        self.assertIsNone(self.why("module:tooling"))
        self.assertIsNone(self.why(f"test:{UNLISTED}", f"test:{LISTED}"))
        aside = f"left out by its reach: no file of it changed since {RUN}, no ask names a test it lists"
        self.assertEqual(self.why(f"test:{UNLISTED}"), aside)  # in its store, not in its step's pattern
        self.assertEqual(self.why("test:tools.tests.test_debug.Debug.test_new"), aside)  # no record yet
        self.assertEqual(self.why("module:python"), aside)
        self.assertEqual(self.why("red"), aside)  # its one red is a test it does not list
        state = store.read(place)
        state["tests"][LISTED]["outcome"] = "failed"
        store.write(place, state)
        self.assertIsNone(self.why("red"))

    def test_build_red(self):
        """H-1: a kept build red runs the step, no file of its reach changed,
        with no ask and with an ask of a test it does not list, so that its
        build runs and the fix, wherever it lies, ends the red; the red ended,
        it is left out as before."""
        place = self.plant()
        state = store.read(place)
        state["build"] = {"since": RUN, "run": RUN, "lines": ["a.cpp(1): error C1083"]}
        store.write(place, state)
        self.assertIsNone(self.why())
        self.assertIsNone(self.why(f"test:{UNLISTED}"))
        self.assertIsNone(self.why("red"))
        state["build"] = None
        store.write(place, state)
        self.assertEqual(self.why(), f"left out by its reach: no file of it changed since {RUN}")
        self.assertEqual(self.why(f"test:{UNLISTED}"),
                         f"left out by its reach: no file of it changed since {RUN}, no ask names a test it lists")


if __name__ == "__main__":
    unittest.main()
