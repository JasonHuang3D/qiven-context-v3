"""The local loop's affected tests (tools/loop.py): the tests of a ctest
step, in tiers 1 and 2, that the change since the step last passed in its
tree reaches by the modules' table, tools/modules.json, never by a list
kept by hand or a reading of the build.

- The check first: each such step runs the modules' check,
  tools/loop_modules.py, on the checkout and the step's tree. An error
  fails the step, which then runs the check as its command, so that its
  log names each file or edge, and no test.
- The record. Each tree keeps RECORD: for each ctest step that passed in
  it, each file of the checkout by its blob id as the job's run began (git's
  index, and git hash-object for a file changed or new), and the table's
  rows. A step that fails leaves its record as it was, so that its changes
  stay selected. A tree new or emptied, or a step with no record in it,
  runs every test; the step's line names why a tree was emptied, the
  toolchain's pins changed among them (tools/loop_run.py's own_tree): the
  record goes with the tree it describes.
- Each changed file selects its module, by the table's paths; a change to
  the table selects the tooling, whose file it is, and each module whose
  row changed; then every module depending on one selected, transitively.
  A file of the no-test row selects nothing. The step runs the tests that
  carry a selected module's label in ctest's own listing
  (--show-only=json-v1), and any test that carries no module's.
- Every test, when a module selected is the whole build's or the shared
  support's, or a changed file is in no module.

The step runs the tests chosen by ctest's --tests-from-file, a file kept in
the tree beside the record; with none chosen it does not run. Its line
names the selection: how many of the step's tests, and the modules chosen,
each changed or depending on which.

Loaded by runpy.run_path, given tools/loop_run.py's names.
"""

import json
import os
import runpy
import subprocess
import sys
import tempfile
from pathlib import Path

RECORD = ".qiven3-loop-passed.json"
CHOSEN = ".qiven3-loop-tests-{}.txt"  # the step's slug
TABLE = "tools/modules.json"
CHECK = Path(__file__).resolve().parent / "loop_modules.py"
MODULES = runpy.run_path(str(CHECK))  # the table's one reading: owners(), reach()
EVERY = ("whole-build", "support")  # the layers whose change runs every test
SHOWN = 3  # the files a reason shows at most; a module chosen is always named


def shown(names, most=None):
    """names sorted, joined; given most, the first most and how many more."""
    names = sorted(names)
    more = f" and {len(names) - most} more" if most and len(names) > most else ""
    return ", ".join(names[:most]) + more


def blobs(root, git):
    """{path: blob id} of each file of the checkout, ignored ones apart, as
    its working tree holds it; None when git cannot tell. git runs under
    its own environment (tools/loop_run.py's git_env)."""
    if not git:
        return None
    try:
        with tempfile.TemporaryDirectory(prefix="qiven3-loop-") as tmp:
            return listed(root, git, git_env(tmp, *LOOP_WHO))
    except OSError:
        return None


def listed(root, git, env):
    """blobs() under git's own environment env."""
    def run(*argv, data=None):
        done = git_run(git, argv, cwd=root, env=env, data=data)  # tools/loop_git.py's door
        if done.returncode != 0:
            raise OSError(done.stderr.decode("utf-8", "replace").strip()[-200:])
        return done.stdout

    try:
        out = {}
        for entry in run("ls-files", "-s", "-z").split(b"\0"):
            if entry:
                meta, _, path = entry.partition(b"\t")
                out[path.decode("utf-8", "surrogateescape")] = meta.split()[1].decode("ascii")
        dirty = list(dict.fromkeys(p.decode("utf-8", "surrogateescape")
                                   for p in run("ls-files", "-z", "-m", "-o", "--exclude-standard").split(b"\0") if p))
        here = [p for p in dirty if (Path(root) / p).is_file() and "\n" not in p]
        for path in dirty:
            out.pop(path, None)
        if here:
            ids = run("hash-object", "--stdin-paths", data="\n".join(here).encode("utf-8", "surrogateescape") + b"\n")
            out.update(zip(here, ids.decode("ascii").split()))
        return out
    except (OSError, subprocess.SubprocessError, ValueError, IndexError):
        return None


def choose(table, rows, changed):
    """The modules a change selects, ({module: why}, None); or (None, why)
    for every test. table: the table as the run began; rows: the record's
    {module: row}; changed: the checkout's files changed."""
    now = {m["name"]: m for m in table["modules"]}
    why, files, lost = {}, {}, []
    for path, names in MODULES["owners"](table, changed).items():
        lost += [] if names else [path]
        for name in names:
            why[name] = "changed"
            files.setdefault(name, []).append(path)
    if TABLE in changed:
        why.update({name: "changed in the table" for name in now if rows.get(name) != now[name] and name not in why})
    every = sorted(name for name in why if now[name]["layer"] in EVERY)
    if lost:
        return None, f"{shown(lost, SHOWN)} in no module"
    if every:
        return None, "the whole build or the support: " + ", ".join(
            f"{name} {why[name]}" + (f" ({shown(files[name], SHOWN)})" if name in files else "") for name in every)
    seeds = {name for name in why if now[name]["layer"] != "no-test"}
    reach = MODULES["reach"](table)
    why = {name: why[name] if name in seeds else "depending on " + shown(reach[name] & seeds)
           for name in now if name in seeds or reach[name] & seeds}
    return why, None


def picked(tests, why, names):
    """The step's tests [(name, labels)] that carry a module's label why
    selects, or no module's; and the line's reasons."""
    chosen, unlabelled = [], 0
    for test, labels in tests:
        mine = [label for label in labels if label in names]
        unlabelled += not mine
        if not mine or any(label in why for label in mine):
            chosen.append(test)
    groups = {}
    for name, reason in why.items():
        groups.setdefault(reason, []).append(name)
    reasons = [f"{shown(mods)} {reason}" for reason, mods in groups.items()]
    return chosen, reasons + ([f"{unlabelled} labelled with no module"] if unlabelled else [])


class Selection:
    """One run's affected tests: root the checkout the steps run in, ctest
    the ctest its steps run, env their environment, git git's path."""

    def __init__(self, root, ctest, env, git):
        self.root, self.ctest, self.env, self.git = Path(root), ctest, env, git
        self.sources, self.table, self.steps, self.emptied = None, None, {}, {}

    def start(self, unit, emptied=None):
        """The checkout's files and the table as the unit's run begins;
        emptied, {tree: why}, the trees emptied for it (tools/loop_run.py's
        own_tree)."""
        self.sources = blobs(self.root, self.git)
        try:
            self.table = json.loads((self.root / TABLE).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            self.table = None
        self.emptied = dict(emptied or {})
        unit.notes.clear()

    def check(self, tree):
        """The modules' check of the checkout and the tree: (its argv, its
        error lines, none when it passes)."""
        argv = [sys.executable, "-I", str(CHECK), str(self.root), str((self.root / tree).resolve())]
        try:
            done = subprocess.run(argv, cwd=self.root, env=self.env, stdin=subprocess.DEVNULL, capture_output=True,
                                  timeout=300)
        except (OSError, subprocess.SubprocessError) as error:
            return argv, [f"it could not run: {error}"]
        lines = (done.stdout + done.stderr).decode("utf-8", "replace").splitlines()
        errors = [x.removeprefix("modules: ") for x in lines if x.startswith("modules: ") and not x.endswith(" errors")]
        return argv, (errors or [f"its status {done.returncode}"]) if done.returncode else []

    def listing(self, words):
        """[(name, labels)] of the step's tests by ctest's listing; None
        when it fails."""
        try:
            done = subprocess.run([self.ctest, *words[1:], "--show-only=json-v1"], cwd=self.root, env=self.env,
                                  stdin=subprocess.DEVNULL, capture_output=True, timeout=300)
            data = json.loads(done.stdout)
            return [(t["name"], next((p["value"] for p in t.get("properties", ()) if p["name"] == "LABELS"), []))
                    for t in data.get("tests", [])]
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError):
            return None

    def affected(self, unit, step, tree, words, narrow):
        """The arguments that narrow the step to its affected tests: [] for
        every test, None for none, a tuple for the step's own argv, the
        failing check's; the step's note in unit.notes."""
        key = "\t".join((step.name, *step.ci))
        self.steps[(unit.name, step.name)] = (tree, key)
        if not narrow:
            return []
        argv, errors = self.check(tree)
        if errors:
            more = f", and {len(errors) - 1} more errors in its log" if len(errors) > 1 else ""
            unit.notes[step.name] = f"the modules' check failed, no test run: {errors[0]}{more}"
            return tuple(argv)
        tests = self.listing(words)
        record = load(self.root / tree).get(key)
        if tests is None:
            unit.notes[step.name] = "every test: ctest's listing failed"
            return []
        total = len(tests)
        if self.sources is None:
            why, every = None, "git cannot list the checkout"
        elif self.table is None:
            why, every = None, f"{TABLE} cannot be read"
        elif record is None and tree in self.emptied:
            why, every = None, f"its tree emptied, {self.emptied[tree]}"
        elif record is None:
            why, every = None, "no passing run of the step in this tree"
        else:
            old = record.get("sources", {})
            changed = [p for p in set(old) | set(self.sources) if old.get(p) != self.sources.get(p)]
            why, every = choose(self.table, record.get("table", {}), changed)
        if why is None:
            unit.notes[step.name] = f"every test, {total} of {total}: {every}"
            return []
        chosen, reasons = picked(tests, why, {m["name"] for m in self.table["modules"]})
        unit.notes[step.name] = f"{len(chosen)} of {total} tests: " + ("; ".join(reasons) or "nothing it runs changed")
        if not chosen:
            return None
        if len(chosen) == total:
            return []
        path = (self.root / tree / CHOSEN.format(slug(step.name))).resolve()
        path.write_text("".join(name + "\n" for name in chosen), encoding="utf-8")
        return ["--tests-from-file", str(path)]

    def passed(self, unit, step):
        """The step passed: its record in its tree, when the unit owns it."""
        tree, key = self.steps.get((unit.name, step.name), (None, None))
        if tree is None or tree not in unit.trees or self.sources is None or self.table is None:
            return
        records = load(self.root / tree)
        records[key] = {"sources": self.sources, "table": {m["name"]: m for m in self.table["modules"]}}
        path = self.root / tree / RECORD
        part = path.with_name(path.name + ".part")
        part.write_text(json.dumps(records, sort_keys=True), encoding="utf-8")
        os.replace(part, path)


def load(tree):
    """The tree's records, {step: record}; empty when it has none."""
    try:
        data = json.loads((Path(tree) / RECORD).read_bytes())
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}
