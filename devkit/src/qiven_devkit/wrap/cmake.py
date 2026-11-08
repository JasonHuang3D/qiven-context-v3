"""CMake's wrapper: a tree configured from a preset or a generator, and its
cache read back through CMake's file API; its fake. See this directory's
README.md.
"""

import json
import os
from pathlib import Path

from qiven_devkit.wrap import call
from qiven_devkit.wrap.fake import Fake

CONFIGURE, CACHE = "cmake configure", "cmake cache"
_CLIENT = "client-qiven-devkit"  # this wrapper's query folder under the file API's query/
_QUERY = "cache-v2"  # the file API's cache object, major version 2


class NotConfigured(call.CallError):
    """The tree holds no reply to this wrapper's cache query."""

    answer = "not configured"


class CMake:
    """CMake, as `program` names it, run under `env` with `limit` seconds a call."""

    def __init__(self, program, env, limit):
        self.program, self.env, self.limit = program, env, limit

    def configure(self, source, tree, *, preset=None, generator=None, entries=None):
        """Configures tree from source by exactly one of preset or generator,
        with the cache entries {name: value}; the cache query written first."""
        source, tree = _absolute(CONFIGURE, source), _absolute(CONFIGURE, tree)
        if (preset is None) == (generator is None):
            raise ValueError(f"{CONFIGURE}: give exactly one of preset and generator")
        defined = []
        for name, value in (entries or {}).items():
            if not name or "=" in name:
                raise ValueError(f"{CONFIGURE}: a cache entry's name is empty or holds '=': {name!r}")
            if not isinstance(value, str):
                raise TypeError(f"{CONFIGURE}: {name}: a cache entry's value must be str, not {type(value).__name__}")
            if value != value.strip():
                raise ValueError(f"{CONFIGURE}: {name}: CMake drops the whitespace around a -D value: {value!r}")
            defined.append(f"-D{name}={value}")
        if not source.is_dir():
            raise ValueError(f"{CONFIGURE}: source must be a directory, not {str(source)!r}")
        query = tree / ".cmake" / "api" / "v1" / "query" / _CLIENT
        query.mkdir(parents=True, exist_ok=True)
        (query / _QUERY).write_bytes(b"")
        if preset is not None:
            args = ["--preset", preset, "-B", str(tree), *defined]
        else:
            args = ["-S", str(source), "-B", str(tree), "-G", generator, *defined]
        call.call(CONFIGURE, self.program, args, env=self.env, cwd=source, limit=self.limit)

    def cache(self, tree):
        """{name: value} of every entry of tree's cache, as its last generate
        recorded it, from the file API's reply."""
        tree = _absolute(CACHE, tree)
        reply = tree / ".cmake" / "api" / "v1" / "reply"
        indexes = sorted(reply.glob("index-*.json")) if reply.is_dir() else []
        if not indexes:
            raise NotConfigured(CACHE, f"{tree} holds no file-API reply index: it was not generated with "
                                       f"this wrapper's query in place")
        index = _json(indexes[-1])
        answer = index.get("reply", {}).get(_CLIENT, {}).get(_QUERY) if isinstance(index, dict) else None
        if answer is None:
            raise NotConfigured(CACHE, f"{indexes[-1].name} answers no {_CLIENT}/{_QUERY} query: {tree} was "
                                       f"generated without it")
        if not isinstance(answer, dict) or not isinstance(answer.get("jsonFile"), str):
            raise call.Unreadable(CACHE, f"{indexes[-1].name}: the {_QUERY} reply names no file: {answer!r:.200}")
        read = _json(reply / answer["jsonFile"])
        if not isinstance(read, dict) or read.get("kind") != "cache" or \
                not isinstance(read.get("version"), dict) or read["version"].get("major") != 2 or \
                not isinstance(read.get("entries"), list):
            raise call.Unreadable(CACHE, f"{answer['jsonFile']}: not a cache object of major version 2")
        values = {}
        for entry in read["entries"]:
            if not isinstance(entry, dict) or not isinstance(entry.get("name"), str) or \
                    not isinstance(entry.get("value"), str):
                raise call.Unreadable(CACHE, f"{answer['jsonFile']}: an entry without a name and a string "
                                             f"value: {entry!r:.200}")
            if entry["name"] in values:
                raise call.Unreadable(CACHE, f"{answer['jsonFile']}: the entry {entry['name']!r} twice")
            values[entry["name"]] = entry["value"]
        return values


class FakeCMake(Fake):
    """CMake's fake: its methods, answered by call name; no CMake started."""

    def configure(self, source, tree, *, preset=None, generator=None, entries=None):
        return self._answer(CONFIGURE, source=source, tree=tree, preset=preset, generator=generator,
                            entries=dict(entries or {}))

    def cache(self, tree):
        return self._answer(CACHE, tree=tree)


def _absolute(name, path):
    """path as a Path; a ValueError where it is not absolute."""
    path = Path(os.fspath(path))
    if not path.is_absolute():
        raise ValueError(f"{name}: a path must be absolute, not {str(path)!r}")
    return path


def _json(file):
    """The JSON of a reply file; Unreadable where it cannot be read as one."""
    try:
        return json.loads(file.read_bytes().decode("utf-8"))
    except (OSError, ValueError) as error:  # UnicodeDecodeError is a ValueError
        raise call.Unreadable(CACHE, f"{file.name}: {type(error).__name__}: {error}") from None
