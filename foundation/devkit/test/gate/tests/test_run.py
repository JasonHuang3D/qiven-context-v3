"""run: a step's run over a fake runner (its build, listing and ctest, a
planted JUnit) in a scratch root, no build run. A run wrong in silence carries
a pass, ends a red that never ran green, or loses a failed build's red."""

import json
import tempfile
import unittest
from pathlib import Path
from xml.sax.saxutils import quoteattr

from foundation.devkit.python import build_tree, git, process
from foundation.devkit.test.gate import envelope, key, run, selection, store

TABLE = {"modules": [{"name": "a", "layer": "devkit"}, {"name": "b", "layer": "devkit"}]}
LISTING = {"a.one": ["a"], "a.two": ["a"], "b.three": ["b"]}


def testcase(name, outcome):
    if outcome == "fail":
        return (f"<testcase name={quoteattr(name)} time=\"2.0\" status=\"fail\"><failure message=\"Failed\"/>"
                f"<system-out>status: 1 failed</system-out></testcase>")
    if outcome.startswith("skip"):  # "skip", or "skip:<its reason>"
        return (f"<testcase name={quoteattr(name)} time=\"0.5\" status=\"notrun\">"
                f"<skipped message=\"SKIP_RETURN_CODE=125\"/><system-out>SKIP {name}: "
                f"{outcome.partition(':')[2] or 'no cdb'}\n"
                f"status: 125 skipped</system-out></testcase>")
    return f"<testcase name={quoteattr(name)} time=\"1.5\" status=\"run\"><system-out>status: 0</system-out></testcase>"


class Fake:
    """The runner: a build of `built`'s status, the listing given, a ctest
    that writes the JUnit of `outcomes` (pass by default; absent, no testcase)."""

    def __init__(self, listing=LISTING, outcomes=None, built=0):
        self.listed, self.outcomes, self.built, self.ran = dict(listing), dict(outcomes or {}), built, []
        self.targets = []

    def build(self, tree, preset, source, targets=None):
        self.targets.append(targets)
        lines = ["FAILED: a/x.obj", "a/x.cpp(3): error C2065: y"] if self.built else ["ninja: no work to do."]
        return build_tree.Built(self.built, 0 if not self.built else 1, not self.built, lines)

    def listing(self, tree):
        return dict(self.listed)

    def run(self, tree, names, *, junit_file, log, jobs, repeat):
        self.ran.append(list(names))
        cases = "".join(testcase(n, self.outcomes.get(n, "pass")) for n in names if self.outcomes.get(n) != "absent")
        Path(junit_file).write_text(f'<?xml version="1.0" encoding="UTF-8"?>\n<testsuite>{cases}</testsuite>\n',
                                    encoding="utf-8")
        Path(log).write_text("log\n", encoding="utf-8")
        return 8 if any(not self.outcomes.get(n, "pass").startswith(("pass", "skip")) for n in names) else 0


class Step(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="qiven-gate-run-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.files = key.Files({"a": {"a/x.cpp": "1"}, "b": {"b/y.cpp": "2"}}, {})
        self.count = 0

    def go(self, fake, preset="ci-windows", asks=(), plan=False, known=None, alone=True, one=None, read_files=None):
        self.count += 1
        self.run = f"20261107T1000{self.count:02d}Z-abcd"
        return run.step(self.root, one or run.Step(preset), lambda: fake, run=self.run, gate="commit",
                        argv=["tools/gate.py", "commit"], asks=[selection.ask(a) for a in asks], plan=plan,
                        known=known, read_table=lambda root: TABLE,
                        read_files=read_files or (lambda root, table: self.files), alone=alone)

    def state(self, preset="ci-windows"):
        return store.read(store.place(self.root, preset))

    def line(self, *steps):
        text, status = envelope.answer(self.run, list(steps))
        self.assertLessEqual(len(text.encode("utf-8")), envelope.BOUND)
        return json.loads(text), status

    def test_red_kept_flake_and_fix(self):
        """A red is kept at its key and reported; an ask that runs it green at
        the same code id is a flake; a green at another code id ends it."""
        fake = Fake(outcomes={"a.two": "fail"})
        first = self.go(fake)
        self.assertEqual((first["code"], first["store"], first["counts"]["failed"], first["counts"]["passed"]),
                         ("TESTS_FAILED", "new", 1, 2))
        self.assertEqual(self.state()["tests"]["a.two"]["since"], f"run:{self.run}")
        self.assertEqual(self.line(first)[1], 1)
        self.assertEqual(self.line(first)[0]["data"]["left_out"]["modules"], "no module")
        since = f"run:{self.run}"
        kept = self.go(fake)
        self.assertEqual((kept["code"], kept["selected"]["tests"], kept["left_out"]),
                         ("RED_KEPT", 0, {"tests": 3, "green_at_key": 2, "red_at_key": 1}))
        answer, status = self.line(kept)
        self.assertEqual((status, answer["data"]["red"][0]["test"], answer["data"]["red"][0]["since"],
                          answer["data"]["red"][0]["ran"]), (1, "a.two", since, False))
        self.assertEqual(answer["data"]["left_out"],
                         {"modules": "every module", "tests": 3, "green_at_key": 2, "red_at_key": 1})
        self.assertIn("--again test:a.two", answer["next_action"])
        self.assertEqual(len(fake.ran), 1)
        fake.outcomes = {}
        flaky = self.go(fake, asks=["test:a.two"])
        record = self.state()["tests"]["a.two"]
        self.assertEqual((flaky["code"], fake.ran[-1], record["flaky"], record["flips"], record["since"]),
                         ("TESTS_FLAKY", ["a.two"], True, 1, since))
        self.files = key.Files({"a": {"a/x.cpp": "1"}, "b": {"b/y.cpp": "3"}}, {})
        other = self.go(fake)
        self.assertEqual((other["code"], fake.ran[-1], other["lists"]["changed"]),
                         ("RED_KEPT", ["b.three"], ["b (1 files)"]))
        self.assertEqual(self.line(other)[0]["data"]["left_out"],
                         {"modules": "every module but b", "tests": 2, "green_at_key": 1, "red_at_key": 1})
        self.files = key.Files({"a": {"a/x.cpp": "4"}, "b": {"b/y.cpp": "3"}}, {})
        fixed = self.go(fake)
        self.assertEqual((fixed["code"], fake.ran[-1], self.state()["tests"]["a.two"]["flaky"]),
                         ("PASSED", ["a.one", "a.two"], False))
        self.assertEqual(self.line(fixed)[1], 0)
        self.assertEqual(self.go(fake)["code"], "NOTHING_TO_RUN")

    def test_skip_ends_no_red(self):
        """G-2 replayed: a skip of a red, at its code id or another, keeps it
        red with its since, no flake, the skip and its reason named; a pass at
        another code id ends it; a skip of a green stays green, never passed."""
        fake = Fake(outcomes={"a.two": "fail"})
        self.go(fake)
        since = f"run:{self.run}"
        for flips, flaky in ((0, False), (1, True)):  # a red, then a flake, skipped at the code id it was red at
            fake.outcomes = {"a.two": "skip"}
            same = self.go(fake, asks=["test:a.two"])
            record = self.state()["tests"]["a.two"]
            self.assertEqual((same["code"], same["counts"]["skipped"], same["counts"]["flaky"], record["since"],
                              record["flips"], record["flaky"]), ("RED_KEPT", 1, 0, since, flips, flaky))
            fake.outcomes = {}
            if not flaky:
                self.assertEqual(self.go(fake, asks=["test:a.two"])["code"], "TESTS_FLAKY")
        fake.outcomes = {"a.two": "skip"}
        self.files = key.Files({"a": {"a/x.cpp": "1"}, "b": {"b/y.cpp": "3"}}, {})
        other = self.go(fake, asks=["test:a.two"])
        record = self.state()["tests"]["a.two"]
        self.assertEqual((other["code"], fake.ran[-1], other["counts"]["passed"], other["counts"]["skipped"],
                          record["since"], record["flaky"]), ("RED_KEPT", ["a.two", "b.three"], 1, 1, since, True))
        red = self.line(other)[0]["data"]["red"][0]
        self.assertEqual((red["test"], red["since"], red["ran"], red["skipped"]), ("a.two", since, True, "no cdb"))
        fake.outcomes = {"a.one": "skip"}
        fixed = self.go(fake, asks=["test:a.two", "test:a.one"])
        self.assertEqual((fixed["code"], fixed["counts"]["passed"], fixed["counts"]["skipped"], fixed["red"]),
                         ("PASSED", 1, 1, []))
        self.assertEqual((self.state()["tests"]["a.two"]["since"], self.state()["tests"]["a.one"]["outcome"]),
                         (f"run:{self.run}", "skipped"))

    def test_no_module(self):
        """A test of no module (a fuzz target's cxx;fuzz) is a listing error in
        a keyed step, and run and recorded, no module, in a step that runs all."""
        fake = Fake(listing={**LISTING, "fuzz.t": ["cxx", "fuzz"]})
        keyed = self.go(fake, preset="fuzz")
        self.assertEqual((keyed["code"], fake.ran), ("BUILD_FAILED", []))
        self.assertIn("fuzz.t: no label names a module", keyed["lists"]["build_lines"][0])
        whole = self.go(fake, one=run.Step("fuzz", every=True))
        record = self.state("fuzz")["tests"]["fuzz.t"]
        self.assertEqual((whole["code"], fake.ran[-1], record["modules"], self.state("fuzz")["build"]),
                         ("PASSED", ["a.one", "a.two", "b.three", "fuzz.t"], [], None))

    def test_failed_build_kept(self):
        """A failed build is its configuration's red, its since its first
        failed run, its tests untouched, until the tree builds again."""
        fake = Fake()
        self.go(fake)
        tests = self.state()["tests"]
        fake.built = 1
        failed = self.go(fake)
        since = f"run:{self.run}"
        self.assertEqual((failed["code"], failed["red"][0]["build"], failed["lists"]["build_lines"][0]),
                         ("BUILD_FAILED", "FAILED: a/x.obj", "FAILED: a/x.obj"))
        self.go(fake)
        self.assertEqual((self.state()["build"]["since"], self.state()["tests"]), (since, tests))
        fake.built = 0
        built = self.go(fake)
        self.assertEqual((built["code"], self.state()["build"], built["red"]), ("NOTHING_TO_RUN", None, []))

    def test_build_stopped(self):
        """A build past its limit is an interrupted run: INTERRUPTED, no
        transition, state.json untouched; a cache that is no UTF-8 is the
        configuration's failed build, answered, never raised."""
        fake = Fake(outcomes={"a.two": "fail"})
        self.go(fake)
        before = self.state()
        failures = [process.TimedOut("cmake", 7200), UnicodeDecodeError("utf-8", b"\xff", 0, 1, "bad byte")]

        def build(tree, preset, source, targets=None):
            raise failures.pop(0)
        fake.build = build
        stopped = self.go(fake)
        self.assertEqual((stopped["code"], self.state()), ("INTERRUPTED", before))
        journal = store.run_file(store.place(self.root, "ci-windows"), self.run, ".jsonl").read_text(encoding="utf-8")
        self.assertEqual(json.loads(journal.splitlines()[-1])["code"], "INTERRUPTED")
        unread = self.go(fake)
        self.assertEqual((unread["code"], self.state()["tests"]), ("BUILD_FAILED", before["tests"]))
        self.assertIn("bad byte", self.state()["build"]["lines"][0])

    def test_not_started(self):
        """A selected test with no testcase: NOT_STARTED, no record made, new again next run."""
        fake = Fake(outcomes={"b.three": "absent"})
        answer = self.go(fake)
        self.assertEqual(answer["code"], "NOT_STARTED")
        self.assertNotIn("b.three", self.state()["tests"])
        self.assertTrue(any(f.startswith("b.three did not start") for f in answer["lists"]["faults"]))
        fake.outcomes = {}
        self.assertEqual((self.go(fake)["code"], fake.ran[-1]), ("PASSED", ["b.three"]))
        records = self.state()["tests"]
        fake.listed["a.four"] = ["a"]

        def run_refused(tree, names, *, junit_file, log, jobs, repeat):
            raise OSError("ctest: no such file")
        fake.run = run_refused
        refused = self.go(fake)
        self.assertEqual((refused["code"], self.state()["tests"]), ("NOT_STARTED", records))
        self.assertIn("ctest did not start: ctest: no such file", refused["lists"]["faults"])

    def test_plan(self):
        """A plan builds and selects, runs nothing and writes no state.json."""
        fake = Fake()
        planned = self.go(fake, plan=True)
        self.assertEqual((planned["code"], planned["selected"]["tests"], planned["selected"]["unknown"], fake.ran,
                          self.state()), ("PLANNED", 3, 3, [], None))
        self.assertEqual(self.line(planned)[1], 1)
        self.go(fake)
        again = self.go(fake, asks=["module:a"], plan=True)
        self.assertEqual((again["code"], again["selected"]["expected"]), ("PLANNED", 1.5))
        self.assertIn("--again module:a", again["next_action"])

    def test_plan_ends_nothing(self):
        """G-6 replayed: a plan writes its journal and its build's red alone:
        its build's red, a gone test's and a gone configuration's kept and
        reported, none named ended, state.json and that store untouched."""
        self.go(Fake(outcomes={"a.one": "fail"}), preset="asan")
        fake = Fake(outcomes={"b.three": "fail"})
        self.go(fake)
        fake.built = 1
        self.go(fake)
        before = self.state()
        fake.built = 0
        fake.listed.pop("b.three")
        planned = self.go(fake, plan=True, known={"ci-windows"})
        self.assertEqual((planned["code"], self.state(), [r.get("build", r.get("test")) for r in planned["red"]],
                          list(planned["others"])), ("RED_KEPT", before, ["FAILED: a/x.obj", "b.three"], ["asan"]))
        self.assertEqual(planned["lists"]["gone"],
                         ["b.three: listed no more", "configuration asan: in no presets file"])
        self.assertTrue(store.place(self.root, "asan").exists())

    def test_interrupted_keeps_reds(self):
        """G-3 replayed: a ctest stopped once a failed build's tree built again
        answers every kept red, the build's and a gone configuration's among
        them, none named ended, state.json and that store untouched."""
        self.go(Fake(outcomes={"a.one": "fail"}), preset="asan")
        fake = Fake(built=1)
        self.go(fake)
        before = self.state()
        fake.built = 0

        def stopped(tree, names, *, junit_file, log, jobs, repeat):
            raise process.TimedOut("ctest", 7200)
        fake.run = stopped
        answer = self.go(fake, known={"ci-windows"})
        self.assertEqual((answer["code"], self.state(), [r.get("build") for r in answer["red"]],
                          list(answer["others"])), ("INTERRUPTED", before, ["FAILED: a/x.obj"], ["asan"]))
        self.assertEqual(answer["lists"]["gone"], ["configuration asan: in no presets file"])
        self.assertTrue(store.place(self.root, "asan").exists())

    def test_git_failed(self):
        """G-7 replayed: git failing mid-step is INTERNAL with its answer, the
        journal ended."""
        def failed(root, table):
            raise git.GitFailed("git ls-files: exit 128")
        answer = self.go(Fake(), read_files=failed)
        self.assertEqual((answer["code"], answer["lists"]["faults"], self.line(answer)[0]["code"]),
                         ("INTERNAL", ["git ls-files: exit 128"], "INTERNAL"))
        where = store.place(self.root, "ci-windows")
        journal = store.run_file(where, self.run, ".jsonl").read_text(encoding="utf-8").splitlines()
        self.assertEqual((json.loads(journal[-1])["kind"], store.unended(where)), ("end", []))

    def test_gone_and_unended(self):
        """A test listed no more is named once with its red, its record dropped;
        a run that never ended is named once."""
        fake = Fake(outcomes={"b.three": "fail"})
        self.go(fake)
        where = store.place(self.root, "ci-windows")
        dead = "20261107T095959Z-dead"
        with store.Journal(where, dead) as planted:
            planted.write("header", run=f"run:{dead}")
        fake.listed.pop("b.three")
        answer = self.go(fake)
        self.assertEqual(answer["lists"]["gone"], ["b.three: listed no more, its red (failed) ended by its going"])
        self.assertIn(f"run:{dead} did not end; nothing taken from it, its asks neither", answer["lists"]["faults"])
        self.assertEqual((answer["code"], sorted(self.state()["tests"])), ("NOTHING_TO_RUN", ["a.one", "a.two"]))
        self.assertEqual(self.go(fake)["lists"]["faults"], [])
        journal = store.run_file(where, self.run, ".jsonl").read_text(encoding="utf-8").splitlines()
        self.assertEqual((json.loads(journal[0])["kind"], json.loads(journal[-1])["kind"]), ("header", "end"))

    def test_ask_no_test(self):
        self.go(Fake())
        self.assertEqual(self.go(Fake(), asks=["test:none"])["code"], "NO_TESTS")
        self.assertEqual((self.go(Fake(listing={}))["code"], self.state()["tests"]), ("NO_TESTS", {}))

    def test_ask_across_steps(self):
        """In a gate of several steps an ask is NO_TESTS only where no step
        lists it: a step that does not list it still runs its selection."""
        self.go(Fake())
        self.go(Fake(listing={"c.four": ["b"]}), preset="asan")
        fake = Fake()
        daily = self.go(fake, asks=["test:c.four"], alone=False)
        self.assertEqual((daily["code"], daily["unmatched"], fake.ran), ("NOTHING_TO_RUN", ["test:c.four"], []))
        other = self.go(Fake(listing={"c.four": ["b"]}), preset="asan", asks=["test:c.four"], alone=False)
        self.assertEqual((other["code"], other["unmatched"], other["counts"]["passed"]), ("PASSED", [], 1))
        answer, status = self.line(daily, other)
        self.assertEqual((answer["code"], status), ("PASSED", 0))
        answer, _ = self.line(daily, {**other, "changed": ["b"]})  # a step that leaves out none names no "but"
        self.assertEqual((answer["data"]["left_out"]["modules"], other["left_out"]["tests"]), ("every module", 0))
        nowhere = self.go(Fake(), asks=["test:none"], alone=False)
        answer, status = self.line(nowhere, other)
        self.assertEqual((answer["code"], status), ("PASSED", 0))
        again = self.go(Fake(listing={"c.four": ["b"]}), preset="asan", asks=["test:none"], alone=False)
        answer, status = self.line(nowhere, again)
        self.assertEqual((answer["code"], status), ("NO_TESTS", 1))
        self.assertIn("--again test:none names no test of any step", answer["next_action"])

    def test_targets_and_reach(self):
        """A step's targets reach the build; the build preset's are read from
        the presets files; a step left out by reach is named in data.reach."""
        (self.root / "CMakePresets.json").write_text(json.dumps({
            "include": ["sub/CMakePresets.json"],
            "buildPresets": [{"name": "plain", "configurePreset": "plain"}]}), encoding="utf-8")
        (self.root / "sub").mkdir()
        (self.root / "sub" / "CMakePresets.json").write_text(json.dumps({"buildPresets": [
            {"name": "analysis", "configurePreset": "analysis", "targets": ["all", "qiven_tidy"]},
            {"name": "one", "configurePreset": "one", "targets": "tidy"}]}), encoding="utf-8")
        self.assertEqual([run.targets(self.root, p) for p in ("analysis", "one", "plain", "none")],
                         [["all", "qiven_tidy"], ["tidy"], None, None])
        fake = Fake()
        answer = self.go(fake, one=run.Step("analysis", targets=["all", "qiven_tidy"]))
        self.assertEqual(fake.targets, [["all", "qiven_tidy"]])
        line, _ = self.line(answer)
        self.assertNotIn("reach", line["data"])
        text, _ = envelope.answer(self.run, [answer], reach={"release": "no file of its reach changed"})
        self.assertEqual(json.loads(text)["data"]["reach"], {"release": "no file of its reach changed"})

    def test_other_configuration(self):
        """Another configuration's red fails this one's answer; a configuration
        in no presets file is gone, its red named and its store dropped."""
        self.go(Fake(outcomes={"a.one": "fail"}), preset="asan")
        mine = self.go(Fake())
        answer, status = self.line(mine)
        self.assertEqual((answer["code"], status, answer["data"]["red"][0]["preset"]), ("RED_KEPT", 1, "asan"))
        self.assertIn("tools/gate.py presets --again test:a.one", answer["next_action"])
        gone = self.go(Fake(), known={"ci-windows"})
        self.assertEqual((gone["code"], gone["lists"]["gone"]),
                         ("NOTHING_TO_RUN", ["configuration asan: in no presets file, its 1 reds ended by its going"]))
        self.assertFalse(store.place(self.root, "asan").exists())

    def test_gate_after_every_step(self):
        """G-1 and the on-site's findings replayed: the reason counts every red
        kept, of every configuration, each red with its ask, each step with its
        tree; the code and next action come from the reds after every step, so
        a red a later step ends fails no gate."""
        self.go(Fake(outcomes={"a.one": "fail", "a.two": "fail"}), preset="asan")
        answer, _ = self.line(self.go(Fake(outcomes={"b.three": "fail"})))
        self.assertTrue(answer["data"]["reason"].endswith("; 3 red kept"), answer["data"]["reason"])
        self.assertEqual([r["ask"] for r in answer["data"]["red"]],
                         [f"{envelope.COMMAND} presets --again test:{name}" for name in ("b.three", "a.one", "a.two")])
        self.files = key.Files({"a": {"a/x.cpp": "5"}, "b": {"b/y.cpp": "6"}}, {})
        structure = self.go(Fake(), alone=False)
        asan = self.go(Fake(), preset="asan", alone=False)
        self.assertEqual((structure["code"], asan["code"]), ("RED_KEPT", "PASSED"))
        answer, status = self.line(structure, asan)
        self.assertEqual((answer["code"], status, answer["data"]["red"], answer["next_action"]),
                         ("PASSED", 0, [], None))
        self.assertEqual([(s["preset"], s["tree"]) for s in answer["data"]["steps"]],
                         [("ci-windows", "build/ci-windows"), ("asan", "build/asan")])

    def test_skips_counted(self):
        """The on-site's finding replayed: past 10 reasons, every further
        reason's skips counted in data.more, 12 reasons of 2 skips each."""
        listing = {f"a.s{n:02d}": ["a"] for n in range(24)}
        outcomes = {name: f"skip:why {n // 2}" for n, name in enumerate(listing)}
        answer, _ = self.line(self.go(Fake(listing=listing, outcomes=outcomes)))
        self.assertEqual((len(answer["data"]["skipped"]), answer["data"]["more"]), (10, {"skipped": 4}))
        self.assertEqual(answer["data"]["counts"]["skipped"], 24)
