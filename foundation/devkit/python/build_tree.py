"""The build tree, for the repository's Python: the one reader of the
checkout's root, of a build directory's cache, of the build's tools and of
its tests' listing. This directory's README states each member's call on
each system.

ROOT is the checkout's root, the directory this file stands three below.
options(build) is the cache of the build directory build, CMakeCache.txt, as
CMake wrote it: each entry's value by its name; None where build holds no
cache, which is no configured build.
cmake(path) and ctest(path) find the build's tools: on the search path path
where it is given; else the ctest QIVEN_CTEST names, the one that runs the
test, and the cmake beside it; else the one on PATH; None where none is.
tests(build) is ctest's listing of build: each test's name to its Test, its
command and its properties; ListingFailed, one line, where it cannot be had.
run_build(build, ...) and run_tests(build, ...) run a build and tests of build
under a limit, from ROOT, and read what CMake, Ninja and CTest print: Built and
Results.
"""

import json
import os
import re
import tempfile
from collections import namedtuple
from pathlib import Path

from foundation.devkit.python import process
from foundation.devkit.python import status as status_table

ROOT = Path(__file__).resolve().parents[3]
_CACHE = "CMakeCache.txt"
_CTEST = "QIVEN_CTEST"  # the ctest that runs a test, set by harness' ctest_tests.cmake
_RESULT = re.compile(r"Test +#\d+: (\S+) +\.*\s*\**(.*?)\s+[\d.]+ sec\s*$")  # ctest's line of a test's result
_STEP = re.compile(r"\[\d+/\d+\]")  # Ninja's line of a step
_GLOBS = "Re-checking globbed directories..."  # the step of every build that checks CONFIGURE_DEPENDS globs, no rebuild
_NO_WORK = "ninja: no work to do."

# A test of ctest's listing: its command line, a list of strings, empty where
# ctest gives none; its properties, {name: value}, as ctest's JSON holds them.
Test = namedtuple("Test", "command properties")

# A build run: its status; steps, Ninja's steps, the globs' check none and a
# regenerating build's CMake rerun one; no_work, true where no step ran and
# Ninja found no work to do; lines, every line it printed but Ninja's step
# lines (configure's or a regenerating build's CMake output, each FAILED: block,
# Ninja's last line).
Built = namedtuple("Built", "status steps no_work lines")

# A test run: ctest's status; passed, how many; failed and skipped, the names,
# in the order ctest ended them.
Results = namedtuple("Results", "status passed failed skipped")


class ListingFailed(Exception):
    """ctest's listing of a build could not be had: one line."""


def options(build):
    """The entries of build's cache, {name: value}, each value the text after
    the entry's first "=", as CMake wrote it; None where build has no cache.
    A cache that cannot be read raises the system's OSError, and one that is
    no UTF-8 UnicodeDecodeError."""
    try:
        text = (Path(build) / _CACHE).read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    found = {}
    for line in text.splitlines():
        if line.startswith(("#", "//")):
            continue
        name, sep, value = line.partition("=")
        if sep and ":" in name:  # NAME:TYPE=VALUE, the one form CMake writes
            found[name.split(":", 1)[0]] = value
    return found


def ctest(path=None):
    """The ctest on the search path path where it is given; else the one
    QIVEN_CTEST names, else the one on PATH; None where none is."""
    if path is not None:
        return process.find("ctest", path=path)
    return os.environ.get(_CTEST) or process.find("ctest")


def cmake(path=None):
    """The cmake on the search path path where it is given; else the one
    beside the ctest QIVEN_CTEST names, else the one on PATH; None where none
    is."""
    if path is not None:
        return process.find("cmake", path=path)
    beside = os.environ.get(_CTEST)
    return (process.find("cmake", path=os.path.dirname(beside)) if beside else None) or process.find("cmake")


def tests(build, args=(), tool=None, env=None):
    """{name: Test} of ctest's listing of the build directory build, by
    --show-only=json-v1, args after it (a selection: -L, -R, -E...); tool the
    ctest, else ctest()'s; env its environment, else this process's.
    ListingFailed where no ctest is found, it cannot start, it passes
    process's limit, it fails, or its answer is no listing."""
    tool = tool or ctest()
    if not tool:
        raise ListingFailed(f"no ctest: none named by {_CTEST} or on PATH")
    shown = f"ctest --show-only of {build}"
    try:
        done = process.run([tool, "--test-dir", str(build), "--show-only=json-v1", *args], env=env)
    except (OSError, process.TimedOut) as e:
        raise ListingFailed(f"{shown}: {e}") from e
    if done.status != 0:
        said = done.err.decode("utf-8", "replace").strip().splitlines()
        raise ListingFailed(f"{shown}: {status_table.ctest_line(done.status)}" + (f": {said[-1]}" if said else ""))
    try:
        return {t["name"]: Test(t.get("command", []), {p["name"]: p["value"] for p in t.get("properties", ())})
                for t in json.loads(done.out)["tests"]}
    except (ValueError, KeyError, TypeError) as e:  # UnicodeDecodeError is a ValueError
        raise ListingFailed(f"{shown}: its answer is no listing: {type(e).__name__}: {e}") from e


def _run(argv, env, limit):
    """(status, output) of argv from ROOT, stderr merged."""
    done = process.run(argv, cwd=ROOT, env=env, limit=limit, err=process.MERGE, text=True)
    return done.status, done.out


def run_build(build, tool, *, preset, env, limit):
    """Built of the build directory build, absolute or from ROOT: tool, its
    cmake, configures it by the preset preset where one is given, then builds
    it unless that failed; env its environment, limit each run's. A cmake that
    cannot start raises the system's OSError; past limit, process.TimedOut."""
    status, text = 0, ""
    if preset:
        status, text = _run([tool, "--preset", preset, "-B", build], env, limit)
    if status == 0:
        status, more = _run([tool, "--build", build], env, limit)
        text += more
    lines = text.splitlines()
    steps = sum(1 for line in lines if _STEP.match(line) and not line.endswith(_GLOBS))
    return Built(status, steps, not steps and _NO_WORK in text, [line for line in lines if not _STEP.match(line)])


def run_tests(build, tool, names, *, log, jobs, env, limit):
    """Results of the tests names of the build directory build, absolute or
    from ROOT, the names by a file outside the tree: tool, its ctest, runs them
    jobs at once, its output in the file log, a failed test's with it; env its
    environment, limit the run's. A ctest that cannot start raises the system's
    OSError; past limit, process.TimedOut."""
    handle, listed = tempfile.mkstemp(prefix="qiven-tests-", suffix=".txt")
    try:
        with os.fdopen(handle, "w", encoding="utf-8", newline="\n") as out:
            out.write("".join(f"{n}\n" for n in names))
        status, text = _run([tool, "--test-dir", build, "--tests-from-file", listed,
                             "--output-on-failure", "--output-log", log, "-j", jobs], env, limit)
    finally:
        os.remove(listed)
    words = {m[1]: m[2] for m in map(_RESULT.search, text.splitlines()) if m}
    skipped = [n for n, w in words.items() if w.startswith("Skipped") or "Disabled" in w]
    return Results(status, list(words.values()).count("Passed"),
                   [n for n, w in words.items() if w != "Passed" and n not in skipped], skipped)
