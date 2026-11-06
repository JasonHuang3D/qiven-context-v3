"""The test runner: build build/ci-windows first, then run the tests of each module whose
files changed since its tests last ran, by the record
build/ci-windows/changed.json; one answer of plain lines, which names each test failed and each
skipped. The count and the selection take no document: a .md file is no module's changed file. A
row of the no-test layer is recorded, but never counted or named as having no test. ctest's output of each run stays in
build/ci-windows/changed.log, which no other ctest in the tree writes over.

Run as py -3.15 -I tools/changed.py, with no argument.
"""

import hashlib
import json
import os
import sys
import tempfile
import time
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent))  # under -I the checkout is off sys.path; appended, so that the standard library still wins
from foundation.devkit.python import answer as bounded
from foundation.devkit.python import build_tree
from foundation.devkit.python import git
from foundation.devkit.python import process
from foundation.devkit.python import status as status_table
from foundation.devkit.python import system
from foundation.devkit.python import table as module_table
from foundation.devkit.python.build_tree import ROOT

TREE = "build/ci-windows"
PRESET = "ci-windows"  # the tree's, which configures a tree that holds no cache
JOBS = 12  # the tests a run runs at once
RECORD = ROOT / TREE / "changed.json"
LOG = RECORD.with_name("changed.log")  # ctest's output of the runner's last run, the failed tests' with it
VCVARS_VER = "14.44.35207"  # the toolset, the tree's
STEP_SECONDS = 7200  # a step's limit, a build's or a test run's: the longest a foreground call of the runner waits

USAGE = ("usage: py -3.15 -I tools/changed.py\n"
         "  builds build/ci-windows, then runs the tests of the modules changed since their tests\n"
         "  last ran (the record build/ci-windows/changed.json); exit 0 passed or none ran,\n"
         "  1 the build or a test failed, 2 it could not run")


class CannotRun(Exception):
    pass


def named(env, name):
    """env's value of name, read without regard to case, as Windows reads it."""
    return next((value for key, value in env.items() if key.upper() == name.upper()), None)


def vcvars_env():
    """The toolset's environment (QIVEN_VCVARS names its vcvars64.bat, else
    it is found), QIVEN_PYTHON this interpreter."""
    try:
        env = system.toolset_env(VCVARS_VER, os.environ.get("QIVEN_VCVARS"))
    except system.Failed as e:
        raise CannotRun(f"{e} (QIVEN_VCVARS names vcvars64.bat)") from e
    env["QIVEN_PYTHON"] = sys.executable
    return env


def build(env, cmake):
    """(status, the answer's build line, and on a failure the lines of build_tree's Built)."""
    try:
        configured = build_tree.options(ROOT / TREE) is not None
    except (OSError, ValueError) as e:  # UnicodeDecodeError is a ValueError
        raise CannotRun(f"{TREE}'s cache: {e}") from e
    built = build_tree.run_build(TREE, cmake, preset=None if configured else PRESET, env=env, limit=STEP_SECONDS)
    if built.status != 0:
        return built.status, [f"build: failed, {status_table.line(built.status)}", *built.lines]
    if built.no_work:
        return 0, ["build: nothing to rebuild"]
    return 0, [f"build: {built.steps} steps"]


def files(table):
    """({module: {file: SHA-256}} of git's files but the documents, by each file's first module;
    [the files in no module])."""
    paths = sorted({p for p in git.tracked(ROOT, others=True) if not p.endswith(".md")})
    paths = [p for p in paths if (ROOT / p).is_file()]  # a tracked file gone from the worktree is gone
    owner, now, loose = module_table.owners(table, paths), {}, []
    for path in paths:
        if not owner[path]:
            loose.append(path)
        else:
            now.setdefault(owner[path][0], {})[path] = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
    return now, loose


def listing(env, ctest):
    """{test name: [its labels]}, CTest's listing of the tree."""
    try:
        tests = build_tree.tests(ROOT / TREE, tool=ctest, env=env)
    except build_tree.ListingFailed as e:
        raise CannotRun(f"ctest's listing failed: {e}") from e
    return {name: test.properties.get("LABELS", []) for name, test in tests.items()}


def select(tests, changed):
    """The names to run: a changed module's tests, or, when each of its changed files is a Python
    test file, those files' own tests (each whose name begins with the file's dotted path)."""
    chosen = {}
    for module, paths in changed.items():
        own = [name for name, labels in tests.items() if module in labels]
        held = {p: [n for n in own if n.startswith(p[:-3].replace("/", ".") + ".")] for p in paths if p.endswith(".py")}
        if all(held.get(p) for p in paths):
            own = [n for p in paths for n in held[p]]
        chosen.update(dict.fromkeys(own))
    return list(chosen)


def write(record):
    """The record, whole, by a temporary file beside it and os.replace."""
    handle, name = tempfile.mkstemp(dir=RECORD.parent, prefix="changed-", suffix=".tmp")
    with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as out:
        json.dump(record, out, indent=1, sort_keys=True)
    os.replace(name, RECORD)


def answer(lines):
    """(exit status, the answer's lines but the last), filled into lines."""
    env = vcvars_env()
    path = named(env, "PATH")
    cmake, ctest = (build_tree.cmake(path), build_tree.ctest(path)) if path else (None, None)
    if not cmake or not ctest:
        raise CannotRun("cmake or ctest is not on vcvars' PATH")
    try:
        table = module_table.load(ROOT)
    except (OSError, ValueError) as e:  # a table in error, or a file of it that cannot be read
        raise CannotRun(str(e) if isinstance(e, ValueError) else f"the table: {e}") from e
    status, built = build(env, cmake)
    lines += built
    if status != 0:
        return 1
    now, loose = files(table)
    if loose:
        lines.append(f"in no module, not selected: {', '.join(loose[:20])}" + (f" and {len(loose) - 20} more" if len(loose) > 20 else ""))
    tests, names = listing(env, ctest), {m["name"] for m in table["modules"]}
    record = json.loads(RECORD.read_text(encoding="utf-8")) if RECORD.is_file() else None
    if record is None:
        write(now)
        lines.append("record: none before; written from the files as they stand, taken as tested by decision; no test run")
        changed, chosen, code = {}, [], 0
    else:
        changed = {m: sorted(p for p in set(now.get(m, {})) | set(record.get(m, {}))
                             if now.get(m, {}).get(p) != record.get(m, {}).get(p)) for m in sorted(set(now) | set(record))}
        changed = {m: paths for m, paths in changed.items() if paths}
        lines.append("changed: " + (", ".join(f"{m} ({len(p)} files)" for m, p in changed.items()) or "none"))
        no_test = {m["name"] for m in table["modules"] if m["layer"] == "no-test"}  # a row of no test by design
        counted = [m for m in changed if m not in no_test]
        bare = [m for m in counted if not any(m in labels for labels in tests.values())]
        if bare:  # its cases lie in another module's row, or it has none: named, never silent
            lines.append(f"no test under its label, none run: {', '.join(bare)}")
        chosen, code = select(tests, changed), 0
        if chosen:
            start = time.monotonic()
            ran = build_tree.run_tests(TREE, ctest, chosen, log=LOG, jobs=JOBS, env=env, limit=STEP_SECONDS)
            seconds = time.monotonic() - start
            failed, skipped = ran.failed, ran.skipped
            lines.append(f"tests: {len(chosen)} of {', '.join(counted)}: {ran.passed} passed, {len(failed)} failed, "
                         f"{len(skipped)} skipped, {seconds:.1f} s")
            if skipped:
                cut = f" and {len(skipped) - 20} more" if len(skipped) > 20 else ""
                lines.append(f"skipped: {', '.join(skipped[:20])}{cut}")
            if failed or ran.status != 0:
                cut = f" and {len(failed) - 20} more" if len(failed) > 20 else ""
                lines.append(f"failed: {', '.join(failed[:20]) + cut or status_table.ctest_line(ran.status)}, "
                             f"the log {TREE}/{LOG.name}")
                code = 1
        else:
            lines.append("tests: none run")
        for module in changed:  # recorded whatever ctest's result
            if module in now:
                record[module] = now[module]
            else:
                record.pop(module, None)
        if changed:
            write(record)
    kept = sorted({label for labels in tests.values() for label in labels if label in names} - set(changed))
    lines.append(f"left out: {len(tests) - len(chosen)} of {len(tests)} tests, of the modules unchanged since "
                 f"their tests last ran: {', '.join(kept) or 'none'}")
    return code


def main(argv):
    if argv:
        print(USAGE)
        return 0 if argv == ["--help"] else 2
    lines = []
    try:
        code = answer(lines)
    except (CannotRun, git.GitFailed, process.TimedOut) as error:
        lines.append(f"cannot run: {error}")
        code = 2
    # the answer's bound, 40 lines with the exit's: the cut named
    lines = bounded.within(lines, 39, f"and {len(lines) - 38} more lines, left out by the answer's bound")
    print("\n".join(lines + [f"exit {code}"]))
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
