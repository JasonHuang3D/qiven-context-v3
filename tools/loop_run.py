"""The local loop's core (tools/loop.py): the answer within its bound, the
steps a job runs, and the kept build trees.

Each tree a job builds in is its own, marked by the job's name in
TREE_MARK, and kept from one run to the next, so that Ninja builds only
what changed. A tree marked by another job, or by none, is emptied before
the job runs in it, so that no file of another build stays (the
clean-binaries check reads every file); a fresh run (tier 3) empties every
tree it runs in. The mark records, below the job's name, the toolchain's
pins the tree was configured under, as the system's side gives them (a
line a pin, its name, a tab, its value): a tree whose pins are not the
run's, or that records none, is emptied before it is configured, since
CMake keeps no cache made by another compiler; its line names the pins
that changed, and its record of passes goes with it, so that its steps
run every test (tools/loop_select.py). Emptying keeps each fuzz target's
corpus, KEEP: libFuzzer grows it, and one run's finds carry to the next.
Only a tree under the checkout's build/ is ever emptied.

Loaded by runpy.run_path: under -I this directory is off sys.path.
"""

import hashlib
import os
import re
import runpy
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATUS = runpy.run_path(str(ROOT / "tools" / "status.py"))
GIT = runpy.run_path(str(ROOT / "tools" / "loop_git.py"))  # git's one door, its count (tools/loop_git.py)
git_which, git_run, git_verdict, GIT_COUNT = GIT["which"], GIT["run"], GIT["verdict"], GIT["COUNT"]
LINES = 100
TAIL = 30
FIRST = 10  # a failing step's first error lines before its tail, at most
WIDTH = 400  # a tail line's characters at most
# An error line, by its tool: a compiler's, a linker's or clang-tidy's
# "error:" (clang, gcc, ld, collect2) or "error C2065:", "error LNK2019:"
# (MSVC, "fatal error" too), and MSVC's "warning C4100:", an error under
# /WX, as are its /analyze findings; ninja's failed edge; CMake's; ctest's
# failed, crashed or timed out test and its list; a Python traceback, its
# exception's line and unittest's failed or erring test; a sanitizer's
# report.
ERROR_LINE = re.compile(r"\berror( [A-Z]+\d+)?:|\bwarning C\d+:|^FAILED: |CMake Error|\*\*\*(Failed|Exception|Timeout)"
                        r"|tests FAILED|^Traceback |^[\w.]*(Error|Exception): |^(FAIL|ERROR): |Sanitizer:")
STEP_TIMEOUT = 45 * 60  # CI's least job limit (windows-analysis's and linux-msan's are 60)
TREE_MARK = ".qiven3-loop-job"
KEEP = ("fuzz/*/corpus",)  # kept through emptying, relative to the tree
LOOP_WHO = ("qiven3 loop", "loop@localhost")  # the author and committer git_env names for the loop's tools
# git_env's settings against git's background work: no automatic maintenance
# after a command, no automatic gc, and either in the foreground if run.
QUIET = {"maintenance.auto": "false", "maintenance.autoDetach": "false", "gc.auto": "0", "gc.autoDetach": "false"}


class CannotRun(Exception):
    """The loop could not run: exit 2."""


RAW = getattr(time, "CLOCK_MONOTONIC_RAW", None)  # Linux's, never slewed


def now():
    """The loop's clock, in seconds: Linux's CLOCK_MONOTONIC_RAW, else the
    performance counter. In WSL2 the guest's CLOCK_MONOTONIC, Python's
    monotonic clock, is slewed by its time daemon and has run 1.3 to 4.6
    percent fast against the host's, so that a Linux step's seconds passed
    the Windows side's "in all" that holds them; the raw clock, never
    slewed, keeps the host's pace, as Windows' performance counter is the
    host's."""
    return time.clock_gettime(RAW) if RAW is not None else time.perf_counter()


class Report:
    """The answer, within its line bound: the last line always has its
    place. A failing step's lines wait for the end, after every step's
    line, where the failing steps share the room left (failures)."""

    def __init__(self, budget):
        self.left = budget - 1  # the last line's place kept
        self.planned = 0  # the lines the callers still plan; read by none since failures waits for the end
        self.dropped = 0
        self.failed = []  # (the step's name, its log) of each failing step, in order
        self.unshown = 0  # failing steps the bound left with no line

    def plan(self, count):
        self.planned += count

    def line(self, text, planned=False):
        if planned:
            self.planned = max(0, self.planned - 1)
        if self.left > 0:
            print(text[:WIDTH], flush=True)
            self.left -= 1
        else:
            self.dropped += 1

    def last(self, text):
        """The failing steps' lines, then the last line."""
        self.failures()
        out = [f"{self.dropped} lines"] if self.dropped else []
        out += [f"{self.unshown} failing steps' lines"] if self.unshown else []
        if out:
            text += f" ({' and '.join(out)} left out by the bound)"
        print(text[:WIDTH], flush=True)

    def fail(self, name, log):
        """A failing step, its lines shown by last."""
        self.failed.append((name, log))

    def failures(self):
        """Each failing step's first error lines before its last lines, in
        the room every step's line left, shared by turns: first each step's
        error lines, as many of FIRST as the room holds (its last lines when
        it has none), each step's first line before any step's second; then
        each step's last lines likewise, as many of TAIL. Each part under its
        own line, naming the step; the error lines are those before the
        tail, the log's first line, the command run_steps wrote, apart."""
        found = []
        for name, log in self.failed:
            try:
                lines = log.read_bytes().decode("utf-8", "replace").splitlines()
            except OSError as e:
                self.line(f"{name}: its log cannot be read: {e}")
                continue
            found.append((name, lines, [i for i, text in enumerate(lines) if i and ERROR_LINE.search(text)]))
        self.failed = []
        own = [min(FIRST, len(errors)) for _, _, errors in found]  # each step's error lines at most
        tails = [min(TAIL, len(lines)) for _, lines, _ in found]
        later = [t if k else 0 for k, t in zip(own, tails)]  # the tails of the steps with error lines
        firsts, rest = [0] * len(found), [0] * len(found)
        room = share(self.left, firsts, [k or t for k, t in zip(own, tails)])  # no error line: its tail first
        room = share(room, rest, later)
        errs = [f if k else 0 for k, f in zip(own, firsts)]

        def picked():
            """Each step's error lines shown: those before its tail, as many as granted."""
            sizes = [r if k else f for k, f, r in zip(own, firsts, rest)]
            return sizes, [[i for i in e if i < len(lines) - s][:n] for (_, lines, e), s, n in zip(found, sizes, errs)]

        for _ in range(2):  # an error line within the tail shows there: its place goes to the tails
            sizes, shown = picked()
            room += sum(n - len(s) + (1 if n and not s else 0) for n, s in zip(errs, shown))
            errs = [len(s) for s in shown]
            room = share(room, rest, later)
        sizes, shown = picked()  # a place freed now stays unused
        for (name, lines, errors), size, chosen_lines in zip(found, sizes, shown):
            if chosen_lines:
                self.line(f"{name}: its first {len(chosen_lines)} of {len(errors)} error lines"
                          f"{', before its tail' if size else ''}:")
                for i in chosen_lines:
                    self.line(f"  ! {i + 1}: {lines[i]}")
            if size:
                self.line(f"{name}: its last {size} of {len(lines)} lines:")
                for text in lines[len(lines) - size:]:
                    self.line("  | " + text)
        self.unshown += sum(1 for size, s in zip(sizes, shown) if not size and not s)


def share(room, grants, tops):
    """Lines granted by turns: each part below its top one more line a turn
    while the room lasts, a part's first line costing its own head line
    too. Grows grants in place; returns the room left."""
    grew = True
    while grew:
        grew = False
        for i, top in enumerate(tops):
            cost = 1 if grants[i] else 2
            if grants[i] < top and room >= cost:
                grants[i] += 1
                room -= cost
                grew = True
    return room


def seconds(start):
    return f"{now() - start:.1f} s"


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def chosen(steps, phase):
    return [s for s in steps if phase == "test" or s.phase == "build"]


def before(step):
    """The steps that must have passed before this one runs."""
    return () if step.after is None else (step.after,) if isinstance(step.after, str) else tuple(step.after)


def run_steps(report, job, steps, phase, command, logs, cwd, env, notes=None, passed=None):
    """Each step of the phase, its line and, on a failure, its tail. command
    maps a step to its argv, or to None for a step left with no test to
    run, which is skipped as passing. notes: a step's note, by its name,
    for its line; passed(step) is told of each step that passed. Returns
    (passed, failed, skipped)."""
    notes = {} if notes is None else notes
    chosen_steps = chosen(steps, phase)
    outcome = {}
    counts = [0, 0, 0]
    logs.mkdir(parents=True, exist_ok=True)
    for old in logs.glob("*.log"):
        old.unlink()
    for index, step in enumerate(chosen_steps, 1):
        failed = [name for name in before(step) if outcome.get(name) is not True]
        if failed:
            outcome[step.name] = False
            counts[2] += 1
            report.line(f"{job} {step.name}: skipped, {failed[0]} did not pass", True)
            continue
        log = logs / f"{index:02d}-{slug(step.name)}.log"
        limit = STEP_TIMEOUT if step.timeout is None else step.timeout or None
        step_env, count_dir = git_counted(env, logs / f"{index:02d}-{slug(step.name)}.git")
        start = now()
        try:
            argv = command(step)
            if argv is None:
                outcome[step.name] = True
                counts[2] += 1
                report.line(f"{job} {step.name}: not run, {notes.get(step.name, 'no test to run')}", True)
                continue
            with open(log, "wb") as out:
                out.write(("$ " + " ".join(argv) + "\n").encode("utf-8"))
                out.flush()
                status = subprocess.run(argv, cwd=cwd, env=step_env, stdin=subprocess.DEVNULL, stdout=out,
                                        stderr=subprocess.STDOUT, timeout=limit).returncode
        except subprocess.TimeoutExpired:
            status = f"timed out after {limit} s"
        except OSError as e:
            raise CannotRun(f"{job} {step.name}: cannot start: {e}") from e
        note = f"; {notes[step.name]}" if step.name in notes else ""
        if count_dir is not None and step.ci[:1] == ("ctest",):  # a test step's git, named on its line
            starts, fault = git_verdict(count_dir)
            note += f"; git {starts}"
            status = fault if status == 0 and fault else status
        outcome[step.name] = status == 0
        if status == 0:
            counts[0] += 1
            report.line(f"{job} {step.name}: pass in {seconds(start)}{note}", True)
            if passed is not None:
                passed(step)
        else:
            counts[1] += 1
            namer = STATUS["ctest_line" if step.ci[:1] == ("ctest",) else "line"]  # a ctest step's status is ctest's
            what = status if isinstance(status, str) else namer(status)
            report.line(f"{job} {step.name}: FAIL, {what}, in {seconds(start)}{note}; log {log}", True)
            report.fail(f"{job} {step.name}", log)
    return counts


def empty(root, tree):
    """The tree emptied, KEEP's directories within it kept: moved aside
    beside it, then back. Refuses a tree outside root's build/."""
    build = (root / "build").resolve()
    tree = tree.resolve()
    if tree == build or not tree.is_relative_to(build):
        raise CannotRun(f"{tree} is outside {build}: the loop empties no other tree")
    aside = tree.parent / (tree.name + ".kept")
    if aside.exists():
        raise CannotRun(f"{aside} is left from an emptying that stopped: put its contents back into {tree}")
    kept = [p for pattern in KEEP for p in tree.glob(pattern) if p.is_dir()]
    try:
        for path in kept:
            (aside / path.relative_to(tree)).parent.mkdir(parents=True, exist_ok=True)
            path.rename(aside / path.relative_to(tree))
        shutil.rmtree(tree)
        tree.mkdir(parents=True)
        for path in kept:
            path.parent.mkdir(parents=True, exist_ok=True)
            (aside / path.relative_to(tree)).rename(path)
        if aside.exists():
            shutil.rmtree(aside)
    except OSError as e:
        raise CannotRun(f"{tree} cannot be emptied: {e}") from e
    return len(kept)


def marked(marker):
    """The mark's job and the pins it records, {name: value}; (None, {})
    with no mark."""
    try:
        lines = marker.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None, {}
    pins = dict(x.split("\t", 1) for x in lines[1:] if "\t" in x)
    return (lines[0].strip() or None) if lines else None, pins


def pins_changed(had, pins):
    """Why the pins a tree records are not the run's; None when they are,
    or when the run gives none (pins None)."""
    if pins is None or had == pins:
        return None
    if not had:
        return "it records no toolchain pins"
    names = sorted(k for k in had.keys() | pins.keys() if had.get(k) != pins.get(k))
    more = f" and {len(names) - 3} more" if len(names) > 3 else ""
    return f"the toolchain's pins changed: {', '.join(names[:3])}{more}"


def own_tree(report, root, tree, job, fresh, pins=None):
    """The job's tree, kept when it is the job's own, configured under the
    run's pins ({name: value}; None: not compared), and the run is not
    fresh; else emptied first. Marked as the job's, with the pins. Returns
    why it was emptied, or None."""
    marker = tree / TREE_MARK
    held, had = marked(marker)
    changed = pins_changed(had, pins) if held == job else None
    why = None
    if tree.exists() and (fresh or held != job or changed):
        kept = empty(root, tree)
        why = f"it held {held or 'a build of no job of the loop'}" if held != job else \
            "a fresh run" if fresh else changed
        corpus = f", {kept} corpus directories kept" if kept else ""
        report.line(f"{job} tree: {tree} emptied, {why}{corpus}", True)
    else:
        report.line(f"{job} tree: {tree} {'kept, its own' if held else 'new'}", True)
    tree.mkdir(parents=True, exist_ok=True)
    pins = had if pins is None else pins
    marker.write_bytes("".join([job + "\n", *(f"{k}\t{v}\n" for k, v in sorted(pins.items()))]).encode("utf-8"))
    return why


def git_counted(env, directory):
    """(a step's environment, its git count's directory, emptied, or None):
    the count's directory a step's own unless env, an enclosing run's
    (a loop a test runs), names one already, which then counts its git."""
    if env is not None and env.get(GIT_COUNT):
        return env, None
    shutil.rmtree(directory, ignore_errors=True)
    return {**(os.environ if env is None else env), GIT_COUNT: str(directory)}, directory


def drive(report, units, root, logs, command, env, phase="test", pins=None):
    """Each unit (a job, or a suite of tier 3) in turn: its trees owned
    under the toolchain's pins (own_tree), then its steps, command(unit,
    step) giving each step's argv; with the unit's Selection
    (tools/loop_select.py), the checkout's files read as it begins, told
    which trees were emptied and why, and each step that passed recorded.
    Each step runs under env with the unit's own over it (its env, CI's
    job-level values). Returns (passed, failed, skipped)."""
    counts = [0, 0, 0]
    for unit in units:
        emptied = {tree: own_tree(report, root, root / tree, unit.name, unit.fresh, pins) for tree in unit.trees}
        select = getattr(unit, "select", None)
        if select is not None:
            select.start(unit, {tree: why for tree, why in emptied.items() if why})
        got = run_steps(report, unit.name, unit.steps, phase, lambda step, unit=unit: command(unit, step),
                        logs / unit.name, root, {**env, **getattr(unit, "env", {})}, getattr(unit, "notes", None),
                        None if select is None else lambda step, unit=unit, select=select: select.passed(unit, step))
        counts = [a + b for a, b in zip(counts, got)]
    return counts


def planned_lines(units, phase="test"):
    """The lines the units' trees and steps take."""
    return sum(len(unit.trees) + len(chosen(unit.steps, phase)) for unit in units)


def clean_script(root, logs):
    """CI's clean-binaries check, ci.yml's CLEAN_CHECK, written beside the logs."""
    text = (root / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    match = re.search(r"^  CLEAN_CHECK: \|\n((?:    .*\n|\n)+)", text, re.MULTILINE)
    if not match:
        raise CannotRun("ci.yml holds no CLEAN_CHECK")
    script = logs / "clean_check.py"
    logs.mkdir(parents=True, exist_ok=True)
    script.write_text("".join(line[4:] + "\n" for line in match.group(1).splitlines()), encoding="utf-8")
    return script


def file_pins(text):
    """The pins of a pin file's text, tools/linux-pins.txt's or
    windows-pins.txt's form, KIND NAME VERSION SHA256 SOURCE a line, its
    comments apart: {"KIND NAME": "VERSION SHA256 SOURCE"}."""
    fields = [x.split("#", 1)[0].split() for x in text.splitlines()]
    return {" ".join(f[:2]): " ".join(f[2:]) for f in fields if len(f) > 2}


def cmake_pins(root):
    """The pins CMakeLists.txt sets, which QIVEN3_PIN_TOOLS checks at
    configure: {QIVEN3_PIN_NAME: value}."""
    try:
        text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    except OSError as e:
        raise CannotRun(f"CMakeLists.txt cannot be read for its pins: {e}") from e
    return dict(re.findall(r"^set\((QIVEN3_PIN_\w+) ([^)\s]+)\)", text, re.MULTILINE))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def git_env(top, name, email):
    """git's own environment, the loop's tools' and each case's
    (tests/drive/own_git.py), so that git computes the same and succeeds
    whatever the host's git config says: the inherited environment less
    every GIT_ variable (a tool sets its own on purpose, a snapshot's
    GIT_INDEX_FILE), git's system config and attributes off, its global
    config, excludes and attributes each an empty file under top, a
    scratch directory, hooks from an empty directory under it, and, in
    git's command scope, which no config file overrides, each setting a
    host's config could turn against it: core.autocrlf false (the
    checkout's .gitattributes governs line endings), no fsmonitor, no
    signing, the initial branch, and the author and committer named; and
    no background work, so that no git command leaves a process writing in
    a repository after it returns (a newer git detaches the maintenance a
    commit or a fetch starts, an older one gc --auto): no automatic
    maintenance or gc, and either in the foreground should one run."""
    top = Path(top)
    empty, hooks = top / "gitconfig", top / "githooks"
    empty.write_bytes(b"")
    hooks.mkdir(exist_ok=True)
    config = {"core.autocrlf": "false", "core.safecrlf": "false", "core.hooksPath": hooks.as_posix(),
              "core.excludesFile": empty.as_posix(), "core.attributesFile": empty.as_posix(),
              "core.fsmonitor": "false", "commit.gpgsign": "false", "tag.gpgsign": "false",
              "init.defaultBranch": "main", **QUIET}
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(GIT_AUTHOR_NAME=name, GIT_AUTHOR_EMAIL=email, GIT_COMMITTER_NAME=name, GIT_COMMITTER_EMAIL=email,
               GIT_CONFIG_NOSYSTEM="1", GIT_ATTR_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty),
               GIT_CONFIG_COUNT=str(len(config)))
    for i, (key, value) in enumerate(config.items()):
        env[f"GIT_CONFIG_KEY_{i}"], env[f"GIT_CONFIG_VALUE_{i}"] = key, value
    return env


def capture(argv, **kw):
    """The command's status and output; CannotRun when it cannot start."""
    try:
        done = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, timeout=300, **kw)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise CannotRun(f"{Path(argv[0]).name}: {e}") from e
    return done.returncode, done.stdout.decode("utf-8", "replace") + done.stderr.decode("utf-8", "replace")


def git_capture(git, argv, **kw):
    """capture() of git with argv, started by its door (tools/loop_git.py)."""
    try:
        done = git_run(git, argv, **kw)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise CannotRun(f"git: {e}") from e
    return done.returncode, done.stdout.decode("utf-8", "replace") + done.stderr.decode("utf-8", "replace")
