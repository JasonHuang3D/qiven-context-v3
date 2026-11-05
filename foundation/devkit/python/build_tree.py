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
"""

import json
import os
from collections import namedtuple
from pathlib import Path

from foundation.devkit.python import process
from foundation.devkit.python import status as status_table

ROOT = Path(__file__).resolve().parents[3]
CACHE = "CMakeCache.txt"
CTEST = "QIVEN_CTEST"  # the ctest that runs a test, set by the runner's ctest_tests.cmake

# A test of ctest's listing: its command line, a list of strings, empty where
# ctest gives none; its properties, {name: value}, as ctest's JSON holds them.
Test = namedtuple("Test", "command properties")


class ListingFailed(Exception):
    """ctest's listing of a build could not be had: one line."""


def options(build):
    """The entries of build's cache, {name: value}, each value the text after
    the entry's first "=", as CMake wrote it; None where build has no cache.
    A cache that cannot be read raises the system's OSError, and one that is
    no UTF-8 UnicodeDecodeError."""
    try:
        text = (Path(build) / CACHE).read_text(encoding="utf-8")
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
    return os.environ.get(CTEST) or process.find("ctest")


def cmake(path=None):
    """The cmake on the search path path where it is given; else the one
    beside the ctest QIVEN_CTEST names, else the one on PATH; None where none
    is."""
    if path is not None:
        return process.find("cmake", path=path)
    beside = os.environ.get(CTEST)
    return (process.find("cmake", path=os.path.dirname(beside)) if beside else None) or process.find("cmake")


def tests(build, args=(), tool=None, env=None):
    """{name: Test} of ctest's listing of the build directory build, by
    --show-only=json-v1, args after it (a selection: -L, -R, -E...); tool the
    ctest, else ctest()'s; env its environment, else this process's.
    ListingFailed where no ctest is found, it cannot start, it passes
    process's limit, it fails, or its answer is no listing."""
    tool = tool or ctest()
    if not tool:
        raise ListingFailed(f"no ctest: none named by {CTEST} or on PATH")
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
