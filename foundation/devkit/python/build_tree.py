"""The build tree, for the repository's Python: the one reader of the
checkout's root and of a build directory's cache. This directory's README
states each member's call on each system.

ROOT is the checkout's root, the directory this file stands three below.
options(build) is the cache of the build directory build, CMakeCache.txt, as
CMake wrote it: each entry's value by its name; None where build holds no
cache, which is no configured build.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CACHE = "CMakeCache.txt"


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
