"""The test runner: build build/ci-windows first, then run the tests of each module whose
files changed since its tests last ran, by the record
build/ci-windows/changed.json; one answer of plain lines. The count and the selection take no
document: a .md file is no module's changed file. ctest's output of each run stays in
build/ci-windows/changed.log, which no other ctest in the tree writes over.

Run as py -3.15 -I tools/changed.py, with no argument.
"""

import hashlib
import json
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TREE = "build/ci-windows"
RECORD = ROOT / TREE / "changed.json"
LOG = RECORD.with_name("changed.log")  # ctest's output of the runner's last run, the failed tests' with it
VCVARS_VER = "14.44.35207"  # the toolset, the tree's
sys.path.append(str(ROOT))  # under -I the checkout is off sys.path; appended, so that the standard library still wins
from foundation.devkit.python import status as status_table
from foundation.devkit.python import system
# Under -I this directory is off sys.path: the map and git's door by runpy.
MODULES = runpy.run_path(str(ROOT / "tools" / "loop_modules.py"))
DOOR = runpy.run_path(str(ROOT / "tools" / "loop_git.py"))  # git's door, by its file: no literal names git
USAGE = ("usage: py -3.15 -I tools/changed.py\n"
         "  builds build/ci-windows, then runs the tests of the modules changed since their tests\n"
         "  last ran (the record build/ci-windows/changed.json); exit 0 passed or none ran,\n"
         "  1 the build or a test failed, 2 it could not run")
RESULT = re.compile(r"Test +#\d+: (\S+) +\.*\s*\**(.*?)\s+[\d.]+ sec\s*$")  # ctest's line of a test's result
STEP = re.compile(r"\[\d+/\d+\]")  # Ninja's line of a step


class CannotRun(Exception):
    pass


def capture(argv, env=None, merge=True):
    """(status, output) of argv from the checkout's root; stderr merged unless not merge."""
    done = subprocess.run(argv, cwd=str(ROOT), env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT if merge else subprocess.DEVNULL, text=True, errors="replace")
    return done.returncode, done.stdout


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
    """(status, the answer's build line, and on a failure every line the build printed but Ninja's step
    lines: configure's or a regenerating build's CMake output, each FAILED: block, Ninja's last line)."""
    status, text = 0, ""
    if not (ROOT / TREE / "CMakeCache.txt").is_file():
        status, text = capture([cmake, "--preset", "ci-windows", "-B", TREE], env)
    if status == 0:
        status, more = capture([cmake, "--build", TREE], env)
        text += more
    if status != 0:
        return status, [f"build: failed, {status_table.line(status)}",
                        *(line for line in text.splitlines() if not STEP.match(line))]
    if "ninja: no work to do." in text:
        return 0, ["build: nothing to rebuild"]
    return 0, [f"build: {sum(1 for line in text.splitlines() if STEP.match(line))} steps"]


def files(table):
    """({module: {file: SHA-256}} of git's files but the documents, by each file's first module;
    [the files in no module])."""
    git = DOOR["which"]()
    if not git:
        raise CannotRun("git is missing")
    done = DOOR["run"](git, ["ls-files", "-z", "-co", "--exclude-standard"], cwd=ROOT)
    if done.returncode != 0:
        raise CannotRun(f"git ls-files failed: {done.stderr.decode('utf-8', 'replace').strip()[-200:]}")
    paths = sorted({p for p in done.stdout.decode("utf-8").split("\0") if p and not p.endswith(".md")})
    paths = [p for p in paths if (ROOT / p).is_file()]  # a tracked file gone from the worktree is gone
    owner, now, loose = MODULES["owners"](table, paths), {}, []
    for path in paths:
        if not owner[path]:
            loose.append(path)
        else:
            now.setdefault(owner[path][0], {})[path] = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
    return now, loose


def listing(env, ctest):
    """{test name: [its labels]}, CTest's listing of the tree."""
    status, text = capture([ctest, "--test-dir", TREE, "--show-only=json-v1"], env, merge=False)
    if status != 0:
        raise CannotRun(f"ctest's listing failed: {status_table.ctest_line(status)}")
    return {t["name"]: next((p["value"] for p in t.get("properties", ()) if p["name"] == "LABELS"), [])
            for t in json.loads(text)["tests"]}


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


def run_tests(env, ctest, chosen):
    """(ctest's status, {name: its result's word}, seconds), the names by a file outside the tree."""
    handle, name = tempfile.mkstemp(prefix="qiven-changed-", suffix=".txt")
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as out:
            out.write("".join(f"{n}\n" for n in chosen))
        start = time.monotonic()
        status, text = capture([ctest, "--test-dir", TREE, "--tests-from-file", name,
                                "--output-on-failure", "--output-log", str(LOG), "-j", "12"], env)
        return status, {m[1]: m[2] for m in map(RESULT.search, text.splitlines()) if m}, time.monotonic() - start
    finally:
        os.remove(name)


def write(record):
    """The record, whole, by a temporary file beside it and os.replace."""
    handle, name = tempfile.mkstemp(dir=RECORD.parent, prefix="changed-", suffix=".tmp")
    with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as out:
        json.dump(record, out, indent=1, sort_keys=True)
    os.replace(name, RECORD)


def answer(lines):
    """(exit status, the answer's lines but the last), filled into lines."""
    env = vcvars_env()
    cmake, ctest = (shutil.which(tool, path=named(env, "PATH")) for tool in ("cmake", "ctest"))
    if not cmake or not ctest:
        raise CannotRun("cmake or ctest is not on vcvars' PATH")
    table = MODULES["load"](ROOT)
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
        changed, chosen, results, code = {}, [], {}, 0
    else:
        changed = {m: sorted(p for p in set(now.get(m, {})) | set(record.get(m, {}))
                             if now.get(m, {}).get(p) != record.get(m, {}).get(p)) for m in sorted(set(now) | set(record))}
        changed = {m: paths for m, paths in changed.items() if paths}
        lines.append("changed: " + (", ".join(f"{m} ({len(p)} files)" for m, p in changed.items()) or "none"))
        bare = [m for m in changed if not any(m in labels for labels in tests.values())]
        if bare:  # its cases lie in another module's row, or it has none: named, never silent
            lines.append(f"no test under its label, none run: {', '.join(bare)}")
        chosen, results, code = select(tests, changed), {}, 0
        if chosen:
            status, results, seconds = run_tests(env, ctest, chosen)
            words = list(results.values())
            passed = words.count("Passed")
            skipped = sum(1 for w in words if w.startswith("Skipped") or "Disabled" in w)
            failed = [n for n, w in results.items() if w != "Passed" and not (w.startswith("Skipped") or "Disabled" in w)]
            lines.append(f"tests: {len(chosen)} of {', '.join(changed)}: {passed} passed, {len(failed)} failed, "
                         f"{skipped} skipped, {seconds:.1f} s")
            if failed or status != 0:
                cut = f" and {len(failed) - 20} more" if len(failed) > 20 else ""
                lines.append(f"failed: {', '.join(failed[:20]) + cut or status_table.ctest_line(status)}, "
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
    except CannotRun as error:
        lines.append(f"cannot run: {error}")
        code = 2
    if len(lines) > 39:  # the answer's bound, 40 lines with the exit's: the cut named
        lines = lines[:38] + [f"and {len(lines) - 38} more lines, left out by the answer's bound"]
    print("\n".join(lines + [f"exit {code}"]))
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
