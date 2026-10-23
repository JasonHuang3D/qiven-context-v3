"""The local loop's core (tools/loop.py): the answer within its bound, the
steps a job runs, and the kept build trees.

Each tree a job builds in is its own, marked by the job's name in
TREE_MARK, and kept from one run to the next, so that Ninja builds only
what changed. A tree marked by another job, or by none, is emptied before
the job runs in it, so that no file of another build stays (the
clean-binaries check reads every file); a fresh run (tier 3) empties every
tree it runs in. Emptying keeps each fuzz target's corpus, KEEP: libFuzzer
grows it, and one run's finds carry to the next. Only a tree under the
checkout's build/ is ever emptied.

Loaded by runpy.run_path: under -I this directory is off sys.path.
"""

import hashlib
import re
import runpy
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATUS = runpy.run_path(str(ROOT / "tools" / "status.py"))
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
STEP_TIMEOUT = 45 * 60  # CI's job limit
TREE_MARK = ".qiven3-loop-job"
KEEP = ("fuzz/*/corpus",)  # kept through emptying, relative to the tree


class CannotRun(Exception):
    """The loop could not run: exit 2."""


class Report:
    """The answer, within its line bound: the last line always has its
    place, and a failing tail leaves room for the lines still planned."""

    def __init__(self, budget):
        self.left = budget - 1  # the last line's place kept
        self.planned = 0
        self.dropped = 0

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
        if self.dropped:
            text += f" ({self.dropped} lines left out by the bound)"
        print(text[:WIDTH], flush=True)

    def tail(self, log):
        """A failing log's first error lines before its tail, as many of
        FIRST as leave room, then its last lines, as many of TAIL as leave
        room: each part under its own line, the error lines' first."""
        room = self.left - self.planned  # this failure's lines, its parts' own lines included
        if room <= 1:
            return
        try:
            lines = log.read_bytes().decode("utf-8", "replace").splitlines()
        except OSError as e:
            self.line(f"  (the log cannot be read: {e})")
            return
        # the first line, the command run_steps wrote, apart
        errors = [i for i, text in enumerate(lines) if i and ERROR_LINE.search(text)]
        size = min(TAIL, room - 1, len(lines))
        first = min(FIRST, sum(1 for i in errors if i < len(lines) - size), room - 3)
        if first > 0:
            size = min(size, room - first - 2)
            shown = [i for i in errors if i < len(lines) - size][:first]
            self.line(f"  its first {len(shown)} of {len(errors)} error lines, before its tail:")
            for i in shown:
                self.line(f"  ! {i + 1}: {lines[i]}")
        self.line(f"  its last {size} of {len(lines)} lines:")
        for text in lines[len(lines) - size:]:
            self.line("  | " + text)


def seconds(start):
    return f"{time.monotonic() - start:.1f} s"


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
        start = time.monotonic()
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
                status = subprocess.run(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=out,
                                        stderr=subprocess.STDOUT, timeout=limit).returncode
        except subprocess.TimeoutExpired:
            status = f"timed out after {limit} s"
        except OSError as e:
            raise CannotRun(f"{job} {step.name}: cannot start: {e}") from e
        outcome[step.name] = status == 0
        note = f"; {notes[step.name]}" if step.name in notes else ""
        if status == 0:
            counts[0] += 1
            report.line(f"{job} {step.name}: pass in {seconds(start)}{note}", True)
            if passed is not None:
                passed(step)
        else:
            counts[1] += 1
            what = status if isinstance(status, str) else STATUS["line"](status)
            report.line(f"{job} {step.name}: FAIL, {what}, in {seconds(start)}{note}; log {log}", True)
            report.tail(log)
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


def own_tree(report, root, tree, job, fresh):
    """The job's tree, kept when it is the job's own and the run is not
    fresh; else emptied first. Marked as the job's."""
    marker = tree / TREE_MARK
    held = marker.read_text(encoding="utf-8").strip() if marker.is_file() else None
    if tree.exists() and (fresh or held != job):
        kept = empty(root, tree)
        why = "a fresh run" if held == job else f"it held {held or 'a build of no job of the loop'}"
        corpus = f", {kept} corpus directories kept" if kept else ""
        report.line(f"{job} tree: {tree} emptied, {why}{corpus}", True)
    else:
        report.line(f"{job} tree: {tree} {'kept, its own' if held else 'new'}", True)
    tree.mkdir(parents=True, exist_ok=True)
    marker.write_bytes(job.encode("utf-8") + b"\n")


def drive(report, units, root, logs, command, env, phase="test"):
    """Each unit (a job, or a suite of tier 3) in turn: its trees owned,
    then its steps, command(unit, step) giving each step's argv; with the
    unit's Selection (tools/loop_select.py), the checkout's files read as
    it begins, and each step that passed recorded. Returns (passed,
    failed, skipped)."""
    counts = [0, 0, 0]
    for unit in units:
        for tree in unit.trees:
            own_tree(report, root, root / tree, unit.name, unit.fresh)
        select = getattr(unit, "select", None)
        if select is not None:
            select.start(unit)
        got = run_steps(report, unit.name, unit.steps, phase, lambda step, unit=unit: command(unit, step),
                        logs / unit.name, root, env, getattr(unit, "notes", None),
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


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def capture(argv, **kw):
    """The command's status and output; CannotRun when it cannot start."""
    try:
        done = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, timeout=300, **kw)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise CannotRun(f"{Path(argv[0]).name}: {e}") from e
    return done.returncode, done.stdout.decode("utf-8", "replace") + done.stderr.decode("utf-8", "replace")
