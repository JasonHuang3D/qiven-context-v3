"""The local loop's tiers (tools/loop.py): what each way of running runs,
and each step's command in its job's own trees.

- Tier 1, the edit loop: TIER1's one job a system in its kept trees,
  configured without --fresh so that Ninja builds only what changed; its
  tests, then TIER1_STEPS: the structure tests and fast's label on its
  build; each ctest step narrowed by affected() to the affected tests.
- Tier 2, a unit's gate: every job of the system in its kept trees, each
  ctest step narrowed by affected().
- Tier 3, the boundary run: every job of the system from emptied trees,
  each configure with --fresh, every test of each job; then SUITES, once a
  system: the full suite and the fuzz targets, each for a time when one is
  given; a suite of TIMED_ONLY only when one is, its job's own step having
  run its tests. A suite empties only the trees it owns, never one it
  reads.
- build or test, of the jobs named (none: every job of the system), in
  their kept trees, every test of each.

A step's command is CI's (tools/loop_jobs.py), its directory mapped to the
job's own tree by TREES, or to the tree a suite reads by READS, then the
loop's additions: a configure by --preset gets -B, a build by --preset its
tree, a ctest by --preset --test-dir, when the job's tree is not the
preset's binaryDir; a directory CI names is replaced. Each preset's
binaryDir is read from CMakePresets.json.

Run by tools/loop.py, given tools/loop_jobs.py's names (runpy's
init_globals): JOBS, Step, TREES, READS, TIER1, TIER1_STEPS, SUITES,
TIMED_ONLY, CLEAN, FUZZ_TIMED.
"""

import json
from pathlib import Path

WAYS = ("1", "2", "3", "build", "test")
TIERS = ("1", "2", "3")


class Unit:
    """A job, or a suite of tier 3, as a way runs it: its name, steps,
    trees (CI's directory: the job's own, or the tree a suite reads), the
    trees it owns, whether they are emptied and configured with --fresh,
    the steps whose tests affected() may narrow (every ctest step of tiers
    1 and 2), and the run's Selection (tools/loop_select.py), None outside
    a run, with each narrowed step's note."""

    __slots__ = ("name", "system", "steps", "map", "trees", "fresh", "narrow", "way", "select", "notes")

    def __init__(self, name, system, steps, way, presets):
        self.name, self.system, self.steps, self.way = name, system, list(steps), way
        own = TREES.get(name, {})
        self.map = {**own, **READS.get(name, {})}
        self.fresh = way == "3"
        narrowed = way in ("1", "2")
        self.narrow = frozenset(s.name for s in self.steps if s.ci[0] == "ctest" and narrowed)
        self.select, self.notes = None, {}
        used = [directory(step, presets) for step in self.steps]
        self.trees = list(dict.fromkeys(own[d] for d in used if d in own))


def plan(way, system, jobs, fuzz_time=None, root=None):
    """The units the way runs on the system, in order. jobs: the jobs a
    build or test names, already resolved; a tier names its own."""
    presets = binary_dirs(root)
    if way == "1":
        name = TIER1[system]
        return [Unit(name, system, [*JOBS[name][2], *TIER1_STEPS.get(name, ())], way, presets)]
    if way in TIERS:
        jobs = [n for n, job in JOBS.items() if job[0] == system]
    units = [Unit(name, system, JOBS[name][2], way, presets) for name in jobs]
    if way == "3":
        for name, (where, steps) in SUITES.items():
            if where == system and (fuzz_time is not None or name not in TIMED_ONLY):
                if fuzz_time is not None:
                    steps = [timed(s, fuzz_time) if s.ci[:2] == ("ctest", "--preset") and s.name == "fuzz" else s
                             for s in steps]
                units.append(Unit(name, system, steps, way, presets))
    return units


def timed(step, seconds):
    """The fuzz preset's step replaced: each target for a time, from its
    corpus, by tools/loop_fuzz.py; its time the targets', no step limit."""
    return Step(step.name, (FUZZ_TIMED, "build/fuzz", str(seconds)), step.phase, after=step.after, timeout=0)


def binary_dirs(root=None):
    """{preset: its binaryDir relative to the source} for every configure,
    build and test preset of CMakePresets.json."""
    root = Path(root) if root else Path(__file__).resolve().parent.parent
    data = json.loads((root / "CMakePresets.json").read_bytes())
    configure = {p["name"]: p for p in data.get("configurePresets", [])}

    def field(table, name, key):
        preset = table.get(name, {})
        if key in preset:
            return preset[key]
        parents = preset.get("inherits", [])
        for parent in [parents] if isinstance(parents, str) else parents:
            got = field(table, parent, key)
            if got is not None:
                return got
        return None

    out = {}
    for name in configure:
        found = field(configure, name, "binaryDir")
        if found:
            out[("configure", name)] = found.replace("${sourceDir}/", "").replace("${presetName}", name)
    for kind in ("build", "test"):
        table = {p["name"]: p for p in data.get(kind + "Presets", [])}
        for name in table:
            parent = field(table, name, "configurePreset")
            if ("configure", parent) in out:
                out[(kind, name)] = out[("configure", parent)]
    return out


def directory(step, presets):
    """The build directory the step works in, as CI's command names it or
    as its preset's binaryDir; None for a step in none."""
    words = step.ci
    if words[0] in (CLEAN, FUZZ_TIMED):
        return words[1]
    if words[0] == "cmake" and words[1] == "--preset":
        return words[words.index("-B") + 1] if "-B" in words else presets.get(("configure", words[2]))
    if words[0] == "cmake" and words[1] == "--build":
        return presets.get(("build", words[3])) if words[2] == "--preset" else words[2]
    if words[0] == "ctest":
        if "--test-dir" in words:
            return words[words.index("--test-dir") + 1]
        if "--preset" in words:
            return presets.get(("test", words[words.index("--preset") + 1]))
    return None


def local_argv(unit, step, presets):
    """The step's command as the loop runs it in the unit's trees, its tool
    by name: CI's command mapped, then the loop's additions; None for a
    ctest step none of whose tests is affected."""
    words = list(step.ci)
    found = directory(step, presets)
    mine = unit.map.get(found, found)
    if found is not None and mine != found:
        if words[0] in (CLEAN, FUZZ_TIMED):
            words[1] = mine
        elif words[0] == "cmake" and words[1] == "--preset":
            if "-B" in words:
                words[words.index("-B") + 1] = mine
            else:
                words += ["-B", mine]
        elif words[0] == "cmake" and words[1] == "--build":
            if words[2] == "--preset":
                words.insert(2, mine)
            else:
                words[2] = mine
        elif "--test-dir" in words:
            words[words.index("--test-dir") + 1] = mine
        else:
            words += ["--test-dir", mine]
    local = list(step.local)
    if unit.fresh and words[0] == "cmake" and words[1] == "--preset":
        local.insert(0, "--fresh")
    if words[0] == "ctest":
        narrowed = affected(unit, step, mine, words)
        if narrowed is None:
            return None
        local += narrowed
    return [*words, *local]


def affected(unit, step, tree, words=("ctest",)):
    """The seam for the affected tests: the arguments that narrow this
    ctest step, in the tree, to the tests the change affects, in tiers 1
    and 2 (unit.narrow); [] for every test, None for none. The unit's
    Selection notes the step's tree, for its record when it passes, in
    every way that runs tests; with none, every test runs."""
    if unit.select is None:
        return []
    return unit.select.affected(unit, step, tree, list(words), step.name in unit.narrow)
