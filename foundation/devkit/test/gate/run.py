"""A step's run, by the test framework's design, in the order of a run: the
lock before any read; the store, the tools, the table; the build; the
listing; the keys and the code id; the selection; ctest; the JUnit;
state.json; retention; the end line; the step's answer. A refusal writes no
file of its run; a failed build writes only its red to state.json; an
interrupted run skips it. The build tools are reached through a runner, the
real one python's build_tree; its tests plant their own. This directory's
README states each member.
"""

import collections
import json
import re
import shutil
import sys
import time
from collections import namedtuple
from pathlib import Path

from foundation.devkit.python import build_tree
from foundation.devkit.python import process
from foundation.devkit.python import system
from foundation.devkit.python import table as module_table
from foundation.devkit.test.gate import envelope, junit, key, selection, states, store

JOBS = 12  # the tests a run runs at once
STEP_SECONDS = 7200  # a build's or a test run's limit
BUILD_LINES = 20  # the failed build's lines kept in its red
SKIP_REASONS = 10  # the skipped list's reasons, each with its count
_ERROR = re.compile(r"FAILED:|error|CMake Error", re.IGNORECASE)  # a build's line that names its failure

# A step of a gate: its configure preset; source, the directory whose presets
# file names it ("." the root, "foundation" for the tree alone); every, its
# selection all; labels and pattern, regular expressions a test's label or
# name must match, or None; repeat, ctest's until-fail count, or None;
# targets, the build's targets, its build preset's, or None for the default.
Step = namedtuple("Step", "preset source every labels pattern repeat targets")
Step.__new__.__defaults__ = (".", False, None, None, None, None)

# The tools found: env, vcvars' environment; cmake and ctest, their files.
Tools = namedtuple("Tools", "env cmake ctest")


class Missing(Exception):
    """A tool missing: vcvars, cmake, ctest or ninja, the gate's TOOL_MISSING."""


def tree_of(preset):
    """A configuration's tree from the root: build/<preset>."""
    return f"build/{preset}"


def find_tools(version, setup=None):
    """The Tools of the toolset at `version`, vcvars `setup` or found; Missing
    naming what is not there."""
    try:
        env = system.toolset_env(version, setup)
    except system.Failed as reason:
        raise Missing(f"vcvars: {reason}") from reason
    env["QIVEN_PYTHON"] = sys.executable
    path = next((value for name, value in env.items() if name.upper() == "PATH"), None)
    found = {name: process.find(name, path=path) if path else None for name in ("cmake", "ctest", "ninja")}
    absent = [name for name, file in found.items() if not file]
    if absent:
        raise Missing(f"not on vcvars' PATH: {', '.join(absent)}")
    return Tools(env, found["cmake"], found["ctest"])


class Runner:
    """The build tools of a run, by python's build_tree: build, listing, run.
    The first call puts this process in its kill-on-close job."""

    def __init__(self, tools, limit=STEP_SECONDS):
        self.tools, self.limit = tools, limit

    def build(self, tree, preset, source, targets=None):
        """build_tree.Built of the tree, configured by preset from source where
        it holds no cache, its targets built, else its default."""
        system.kill_on_close()
        configured = build_tree.options(build_tree.ROOT / tree) is not None
        return build_tree.run_build(tree, self.tools.cmake, preset=None if configured else preset,
                                    env=self.tools.env, limit=self.limit, source=source, targets=targets)

    def listing(self, tree):
        """{name: [labels]} of the tree's tests; build_tree.ListingFailed."""
        tests = build_tree.tests(build_tree.ROOT / tree, tool=self.tools.ctest, env=self.tools.env)
        return {name: list(test.properties.get("LABELS", [])) for name, test in tests.items()}

    def run(self, tree, names, *, junit_file, log, jobs, repeat):
        """ctest's status over the tests names, its JUnit and its log written."""
        system.kill_on_close()
        return build_tree.run_ctest(tree, self.tools.ctest, names, junit=junit_file, log=log, jobs=jobs,
                                    repeat=repeat, env=self.tools.env, limit=self.limit)


def _presets_files(root):
    """Each presets file's data: the root's CMakePresets.json, then the files it includes."""
    seen, queue = set(), [Path(root) / "CMakePresets.json"]
    while queue:
        path = queue.pop().resolve()
        if path in seen or not path.is_file():
            continue
        seen.add(path)
        data = json.loads(path.read_text(encoding="utf-8"))
        queue += [path.parent / each for each in data.get("include", [])]
        yield data


def presets(root):
    """The names of the configure presets of the root's CMakePresets.json and
    the files it includes, hidden ones out."""
    return {p["name"] for data in _presets_files(root) for p in data.get("configurePresets", [])
            if not p.get("hidden")}


def targets(root, preset):
    """The targets of the build preset of the name preset, over the configure
    preset of that name, in the presets files, a list; None where it names
    none, its tree's default."""
    for data in _presets_files(root):
        for each in data.get("buildPresets", []):
            if each.get("name") == preset and each.get("configurePreset") == preset and each.get("targets"):
                found = each["targets"]
                return [found] if isinstance(found, str) else list(found)
    return None


def _red(name, record, preset, where, ran):
    return {"test": name, "outcome": states.state(record), "ran": ran, "since": record["since"], "preset": preset,
            "flips": record["flips"], "log": str(store.run_file(where, record["since"].removeprefix("run:"), ".log")),
            "modules": record.get("modules", []), "labels": record.get("labels")}


def reds(state, preset, where, ran=(), now=None):
    """The red entries of a state.json: its failed build's, then each red test's,
    `ran` naming the tests that ran in this run, `now` its spelled id."""
    found = []
    if state.get("build"):
        build = state["build"]
        found.append({"build": (build["lines"] or ["no line"])[0], "preset": preset, "since": build["since"],
                      "ran": build["run"] == now,
                      "log": str(store.run_file(where, build["run"].removeprefix("run:"), ".build.log")),
                      "modules": []})
    for name in sorted(state["tests"]):
        record = state["tests"][name]
        if states.is_red(record):
            found.append(_red(name, record, preset, where, name in ran))
    return found


def _changed(now, before):
    """The modules whose files changed since the last run: [(module, count)]."""
    found = []
    for module in sorted(set(now) | set(before)):
        a, b = now.get(module, {}), before.get(module, {})
        count = sum(1 for path in set(a) | set(b) if a.get(path) != b.get(path))
        if count:
            found.append((module, count))
    return found


def _filtered(listed, step):
    return {name: labels for name, labels in listed.items() if envelope.lists((step.labels, step.pattern), name, labels)}


def _drop(where, faults):
    """A gone configuration's store removed, unless a run holds it."""
    try:
        store.lock(where, ["gate: a gone configuration's store dropped"]).release()
        shutil.rmtree(where)
    except (store.Locked, store.Internal, OSError) as reason:
        faults.append(f"{where.name}: its store cannot be dropped: {reason}")


class _Stop(Exception):
    """A step's answer reached before its end: its code and words."""

    def __init__(self, code, words):
        super().__init__(words)
        self.code, self.words = code, words


def step(root, one, tools, *, run, gate, argv, asks=(), plan=False, jobs=JOBS, owners=None,
         command=envelope.COMMAND, known=None, read_table=module_table.load, read_files=key.files,
         clock=time.monotonic, alone=True):
    """The answer of one step `one` of `gate`, a dictionary (the README's
    Members), at the run id `run`: `tools`, a callable answering a runner
    (build, listing, run) or raising Missing; `asks`, selection.Asks; `plan`,
    build and select, run no test; `owners`, {preset: [(gate, labels,
    pattern)]}, the gates that own its reds in order (envelope.owner);
    `known`, the presets that exist, None not to look for a gone configuration;
    `read_table` and `read_files` the table's and git's readers; `alone`, the
    gate's one step, whose ask that names no test of its tree is NO_TESTS:
    else the ask is left in `unmatched` for the gate (envelope.answer)."""
    began, owners, preset, where = clock(), owners or {}, one.preset, store.place(root, one.preset)
    tree = tree_of(preset)
    answer = {"preset": preset, "code": None, "next_action": None, "seconds": 0.0, "journal": None, "reason": "",
              "store": None, "build": {"steps": 0}, "counts": dict.fromkeys(
                  ("passed", "skipped", "failed", "flaky", "not_started"), 0),
              "selected": {"tests": 0, "seconds": 0.0, "expected": 0.0, "unknown": 0},
              "left_out": {"tests": 0, "green_at_key": 0, "red_at_key": 0}, "changed": [],
              "red": [], "others": {}, "lists": {name: [] for name in envelope.LISTS}, "plan": plan,
              "unmatched": [selection.spelled(each) for each in asks if each.kind != "red"]}
    lists, faults = answer["lists"], answer["lists"]["faults"]

    def done(code, words=""):
        answer["code"] = code
        if code in ("RED_KEPT", "TESTS_FLAKY"):
            first = next((r for r in answer["red"] if code != "TESTS_FLAKY" or r.get("outcome") == "flaky"),
                         None) or next((r for p in answer["others"].values() for r in p), None)
            answer["next_action"] = envelope.red_action(first, owners, command) if first else None
        else:
            answer["next_action"] = envelope.action(code, words)
        answer["seconds"] = round(clock() - began, 1)
        return answer

    try:
        held = store.lock(where, argv)
    except store.Locked as locked:
        holder = locked.holder or {}
        return done("STORE_LOCKED", f"{preset}'s store is held by pid {holder.get('pid', '?')} since "
                                    f"{holder.get('start', '?')}: {' '.join(holder.get('argv', []))}; "
                                    f"then {command} {gate}")
    except store.Internal as fault:
        faults.append(str(fault))
        return done("INTERNAL", str(fault))
    with held:
        try:
            state, dead = store.read(where), store.unended(where)
            others = store.others(root, preset)
        except store.Internal as fault:
            faults.append(str(fault))
            return done("INTERNAL", str(fault))
        answer["store"] = "new" if state is None else None
        state = state or store.empty(preset)
        gone_configurations = sorted(p for p in others if known is not None and p not in known)
        for other, its in others.items():
            answer["others"][other] = reds(its, other, store.place(root, other))
        answer["red"] = reds(state, preset, where)
        try:
            runner = tools()
        except Missing as missing:
            return done("TOOL_MISSING", f"{missing}; then {command} {gate}")
        try:
            table = read_table(root)
        except (OSError, ValueError) as error:
            return done("TABLE_ERROR", f"the module table: {error}")
        try:
            return _ran(root, one, runner, table, read_files, run, gate, argv, asks, plan, jobs, answer, state,
                        dead, others, gone_configurations, where, tree, done, command, alone)
        except _Stop as stop:
            return done(stop.code, stop.words)


def _ran(root, one, runner, table, read_files, run, gate, argv, asks, plan, jobs, answer, state, dead, others,
         gone_configurations, where, tree, done, command, alone):
    """The step past its refusals: its journal opened, each point in order."""
    preset, lists = one.preset, answer["lists"]
    faults, spelled = lists["faults"], store.spelled(run)
    answer["journal"] = str(store.run_file(where, run, ".jsonl"))
    with store.Journal(where, run) as journal:
        try:
            for each in dead:  # a run that never ended, named once: its journal ended by this one
                with store.Journal(where, each) as theirs:
                    theirs.write("end", ended=False, ended_by=spelled)
                faults.append(f"{store.spelled(each)} did not end; nothing taken from it, its asks neither")
            (Path(root) / tree).mkdir(parents=True, exist_ok=True)
            tree_id = store.tree_id(Path(root) / tree)
            files = read_files(root, table)
            code = key.code_id(files)
        except store.Internal as fault:
            faults.append(str(fault))
            raise _Stop("INTERNAL", str(fault))
        journal.write("header", run=spelled, gate=gate, preset=preset, tree=tree_id, code=code,
                      argv=[str(word) for word in argv], start=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))

        def end(code_name, words=""):
            """state.json's red, retention and the end line done before: the end line, then the answer."""
            journal.write("end", ended=True, code=code_name)
            return done(code_name, words)

        named_gone = []  # the gone configurations named, dropped at the run's end

        def keep_state():
            try:
                store.write(where, state)
            except store.Internal as fault:
                faults.append(str(fault))
                return False
            kept = reds(state, preset, where)
            cited = [r["since"] for r in kept] + [Path(r["log"]).name.partition(".")[0] for r in kept] + [spelled]
            faults.extend(store.retain(where, cited))
            for other in named_gone:
                _drop(store.place(root, other), faults)
            return True

        def build_failed(lines, words):
            lines = [envelope.bounded.clip(line) for line in lines[:BUILD_LINES]]
            states.build_failed(state, spelled, lines)
            answer["red"] = reds(state, preset, where, (), spelled)
            lists["build_lines"] = lines
            ok = keep_state()
            return end("BUILD_FAILED" if ok else "INTERNAL", words if ok else faults[-1])

        build_log = store.run_file(where, run, ".build.log")
        try:
            built = runner.build(tree, preset, one.source, one.targets)
        except (KeyboardInterrupt, process.TimedOut) as stop:  # an interrupted run: no transition, no state.json
            journal.write("build", status=None, why=f"interrupted: {type(stop).__name__}: {stop}")
            return end("INTERRUPTED", f"stopped in the build of {preset} ({type(stop).__name__}): {command} {gate}")
        except (OSError, UnicodeDecodeError) as failure:  # cmake did not start, or the tree's cache is no UTF-8
            journal.write("build", status=None, why=str(failure))
            return build_failed([str(failure)], f"the build of {preset} did not run: {failure}")
        try:
            build_log.write_text("\n".join(built.lines) + "\n", encoding="utf-8")
        except OSError as reason:
            faults.append(f"{build_log}: {reason}")
        answer["build"] = {"steps": built.steps}
        journal.write("build", status=built.status, steps=built.steps, no_work=built.no_work)
        if built.status != 0:
            said = [line for line in built.lines if _ERROR.search(line)] or built.lines[-1:]
            return build_failed(said, f"the build of {preset} failed: {(said or ['no line'])[0]}; "
                                      f"its output in {build_log}")
        try:
            listed = runner.listing(tree)
            keys, modules = {}, {}
            for name, labels in listed.items():
                try:
                    keys[name] = key.test_key(preset, name, labels, files)
                    modules[name] = key.modules_of(name, labels, files.modules)[0]
                except key.NoModule:
                    if not one.every:  # a test of no module in a keyed step: a listing error
                        raise
                    keys[name], modules[name] = key.bare_key(preset, name), []  # run whole, keyed by nothing
        except (build_tree.ListingFailed, key.NoModule) as error:
            journal.write("listing", failed=str(error))
            return build_failed([str(error)], f"the listing of {preset}: {error}")
        states.build_passed(state)
        answer["red"] = reds(state, preset, where)
        journal.write("listing", tests=len(listed))

        changed = _changed(files.modules, state["modules"])
        answer["changed"] = [module for module, _ in changed]
        lists["changed"] = [f"{module} ({count} files)" for module, count in changed]
        lists["unowned"] = sorted(files.unowned)
        no_test = {row["name"] for row in table["modules"] if row["layer"] == "no-test"}
        labelled = {label for labels in listed.values() for label in labels}
        lists["untested"] = [m for m, _ in changed if m not in no_test and m not in labelled]
        tests = state["tests"]
        gone = states.gone(tests, listed)
        lists["gone"] = [f"{name}: listed no more" + (f", its red ({states.state(tests[name])}) ended by its going"
                                                      if states.is_red(tests[name]) else "") for name in gone]
        for other in gone_configurations:
            count = len(answer["others"].pop(other, []))
            named_gone.append(other)
            lists["gone"].append(f"configuration {other}: in no presets file" +
                                 (f", its {count} reds ended by its going" if count else ""))

        chosen = _filtered(listed, one)
        if not listed or not chosen:
            words = f"the tree {tree} lists no test" if not listed else \
                f"the step {preset} (labels {one.labels}, pattern {one.pattern}) lists no test"
            keep_state()
            return end("NO_TESTS", words)
        picked = selection.choose(chosen, tests, keys, tree_id, asks, one.every)
        for name, why in picked.selected.items():
            journal.write("selected", test=name, reason=why)
        for name, kept in picked.left_out.items():
            journal.write("left_out", test=name, colour=kept["colour"], run=kept["since"])
        names = list(picked.selected)
        expected = selection.plan(names, tests, jobs)
        answer["selected"].update(tests=len(names), expected=expected.seconds, unknown=expected.unknown)
        left = list(picked.left_out.values())
        answer["left_out"] = {"tests": len(left), "green_at_key": sum(k["colour"] == states.GREEN for k in left),
                              "red_at_key": sum(k["colour"] == states.RED for k in left)}
        answer["reason"] = _reason(built, changed, picked, answer["red"])
        answer["unmatched"] = list(picked.unmatched)
        if picked.unmatched and alone:
            keep_state()
            return end("NO_TESTS", f"--again {', '.join(picked.unmatched)} names no test of {preset}")
        if plan:
            if not keep_state():
                return end("INTERNAL", faults[-1])
            again = "".join(f" --again {selection.spelled(one)}" for one in asks)
            return end(_kept(answer) or ("PLANNED" if names else "NOTHING_TO_RUN"),
                       f"{len(names)} tests, about {expected.seconds} s"
                       + (f", {expected.unknown} with no duration yet" if expected.unknown else "")
                       + f": {command} {gate}{again}")

        ran = {}
        if names:
            junit_file, log = store.run_file(where, run, ".xml"), store.run_file(where, run, ".log")
            started = time.monotonic()
            try:
                status = runner.run(tree, names, junit_file=junit_file, log=log, jobs=jobs, repeat=one.repeat)
            except (KeyboardInterrupt, process.TimedOut) as stop:
                return end("INTERRUPTED", f"stopped before ctest returned ({type(stop).__name__}): "
                                          f"{command} {gate}")
            except OSError as failure:  # no JUnit: each selected test NOT_STARTED, no record changed
                faults.append(f"ctest did not start: {failure}")
                status = None
            answer["selected"]["seconds"] = round(time.monotonic() - started, 1)
            ran = junit.read(junit_file, names)
            for name in names:
                journal.write("outcome", test=name, outcome=ran[name].outcome, seconds=ran[name].seconds,
                              why=ran[name].why)
            red_ran = [n for n in names if ran[n].outcome not in junit.GREEN and ran[n].outcome != junit.NOT_STARTED]
            not_started = [n for n in names if ran[n].outcome == junit.NOT_STARTED]
            if (status == 0) != (not red_ran and not not_started):
                faults.append(f"ctest's exit {status} against its JUnit: {len(red_ran)} red, "
                              f"{len(not_started)} not started")
            for name in not_started:
                faults.append(f"{name} did not start: {ran[name].why}")
            for name in names:
                tests[name] = states.after(tests.get(name), keys[name], tree_id, code, spelled, ran[name])
        for name in listed:
            if tests.get(name) is not None:
                tests[name] = {**tests[name], "modules": modules[name], "labels": list(listed[name])}
            else:
                tests.pop(name, None)
        for name in gone:
            tests.pop(name, None)
        state.update(tree=tree_id, code=code, run=spelled, modules=files.modules, unowned=files.unowned)
        counts = answer["counts"]
        for name, outcome in ((n, r.outcome) for n, r in ran.items()):
            if outcome == junit.NOT_STARTED:
                counts["not_started"] += 1
            elif outcome in junit.GREEN:
                counts["flaky" if tests[name]["flaky"] else outcome] += 1
            else:
                counts["failed"] += 1
        reasons = collections.Counter(r.why for r in ran.values() if r.outcome == "skipped")
        lists["skipped"] = [f"{count}: {why}" for why, count in reasons.most_common(SKIP_REASONS)]
        answer["red"] = sorted(reds(state, preset, where, ran, spelled), key=lambda r: not r["ran"])
        answer["reason"] = _reason(built, changed, picked, answer["red"])  # the reds as they stand after the run
        if not keep_state():
            return end("INTERNAL", faults[-1])
        if any(f.startswith("ctest's exit") for f in faults):
            return end("INTERNAL", next(f for f in faults if f.startswith("ctest's exit")))
        if counts["not_started"]:
            first = next(n for n in names if ran[n].outcome == junit.NOT_STARTED)
            return end("NOT_STARTED", f"{first} did not start: {ran[first].why}; ctest's log {log}")
        if counts["failed"]:
            first = next(n for n in names if ran[n].outcome not in junit.GREEN and ran[n].outcome != junit.NOT_STARTED)
            return end("TESTS_FAILED", f"{first} {ran[first].outcome} in {preset}; fixed, {command} {gate}: "
                                       f"{ran[first].why}; its output in {log}")
        if counts["flaky"]:
            return end("TESTS_FLAKY")
        return end(_kept(answer) or ("PASSED" if names else "NOTHING_TO_RUN"))


def _kept(answer):
    """RED_KEPT where a red stands, of this configuration or another, else None."""
    return "RED_KEPT" if answer["red"] or any(answer["others"].values()) else None


def _reason(built, changed, picked, kept):
    """The step's one line of why it ran what it ran."""
    rebuilt = "nothing rebuilt" if built.no_work else f"{built.steps} build steps"
    files = f"{len(changed)} modules changed" if changed else "no module file changed"
    why = collections.Counter(picked.selected.values())
    ran = ", ".join(f"{count} {reason}" for reason, count in sorted(why.items())) or "no test selected"
    red = sum(1 for r in kept if not r.get("ran"))
    return f"{rebuilt}, {files}; {ran}" + (f"; {red} red kept" if red else "")
