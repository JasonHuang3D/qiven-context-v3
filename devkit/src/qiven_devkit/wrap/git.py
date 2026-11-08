"""git's wrapper: each call named, each started once through the call base, its
-z answer decoded in one place; its fake. See this directory's README.md.
"""

from pathlib import Path

from qiven_devkit.wrap import call
from qiven_devkit.wrap.fake import Fake

LISTING, FILES_AT, ATTRIBUTES = "git ls-files", "git cat-file", "git check-attr"


class NoCommit(call.CallError):
    """files_at was given a commit git does not have."""

    answer = "no such commit"


class Git:
    """git, as `program` names it, run under `env` with `limit` seconds a call."""

    def __init__(self, program, env, limit):
        self.program, self.env, self.limit = program, env, limit

    def listing(self, folder):
        """The names of the files under folder that git tracks, and of the
        untracked ones no ignore rule names, from folder, each once."""
        answer = self._call(LISTING, folder, ["ls-files", "-z", "--cached", "--others", "--exclude-standard"])
        return list(dict.fromkeys(_fields(answer)))

    def files_at(self, folder, commit, paths):
        """{path: bytes} of each of paths, from the repository's root, that
        names a file at commit; NoCommit where commit names no commit."""
        paths = list(paths)
        for word in (commit, *paths):
            if "\n" in word:
                raise ValueError(f"{FILES_AT}: a name holding a newline: {word!r}")
        asked = "".join(f"{line}\n" for line in (f"{commit}^{{commit}}", *(f"{commit}:{path}" for path in paths)))
        answer = self._call(FILES_AT, folder, ["cat-file", "--batch"], asked.encode("utf-8"))
        rest = answer.out
        kind, _, rest = _object(answer, rest)
        if kind != "commit":
            raise NoCommit(FILES_AT, f"{commit}: git has no such commit in {folder}")
        files = {}
        for path in paths:
            kind, content, rest = _object(answer, rest)
            if kind == "blob":
                files[path] = content
        if rest:
            raise call.Unreadable(FILES_AT, f"{len(rest)} bytes past the last object asked")
        return files

    def attributes(self, folder, paths, names):
        """{path: {name: value}} of the attributes names of paths, from
        folder, in git's own words."""
        names = list(names)
        data = "".join(f"{path}\0" for path in paths).encode("utf-8")
        answer = self._call(ATTRIBUTES, folder, ["check-attr", "-z", "--stdin", *names], data)
        fields = _fields(answer)
        if len(fields) % 3:
            raise call.Unreadable(ATTRIBUTES, f"{len(fields)} fields, not a multiple of 3 (path, name, value)")
        read = {}
        for path, name, value in zip(fields[::3], fields[1::3], fields[2::3]):
            read.setdefault(path, {})[name] = value
        return read

    def _call(self, name, folder, args, data=None):
        return call.call(name, self.program, args, env=self.env, cwd=folder, limit=self.limit, data=data)


class FakeGit(Fake):
    """Git's fake: its methods, answered by call name; no git started."""

    def listing(self, folder):
        return self._answer(LISTING, folder=folder)

    def files_at(self, folder, commit, paths):
        return self._answer(FILES_AT, folder=folder, commit=commit, paths=list(paths))

    def attributes(self, folder, paths, names):
        return self._answer(ATTRIBUTES, folder=folder, paths=list(paths), names=list(names))


def steady_env(base, directory):
    """base under which git reads a work tree the same whatever the host's
    git configuration says; directory/empty is written, an empty file."""
    empty = Path(directory) / "empty"
    if not empty.is_file() or empty.stat().st_size:
        empty.write_bytes(b"")
    config = {"core.attributesFile": empty.as_posix(), "core.excludesFile": empty.as_posix(),
              "core.fsmonitor": "false"}
    env = {key: value for key, value in base.items() if not key.upper().startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_ATTR_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty),
               GIT_CONFIG_COUNT=str(len(config)))
    for i, (key, value) in enumerate(config.items()):
        env[f"GIT_CONFIG_KEY_{i}"], env[f"GIT_CONFIG_VALUE_{i}"] = key, value
    return env


def _fields(answer):
    """A -z answer's fields, as text: the one place one is decoded."""
    out = answer.out
    if not out:
        return []
    if not out.endswith(b"\0"):
        raise call.Unreadable(answer.name, f"its -z answer does not end with NUL: ...{out[-40:]!r}")
    try:
        return [field.decode("utf-8") for field in out[:-1].split(b"\0")]
    except UnicodeDecodeError as error:
        raise call.Unreadable(answer.name, f"a field of its -z answer is not UTF-8: {error}") from None


def _object(answer, rest):
    """(kind, content, rest) of the next object of a cat-file --batch answer:
    kind None where git names it missing or ambiguous."""
    head, newline, rest = rest.partition(b"\n")
    if not newline:
        raise call.Unreadable(answer.name, f"an object's line has no end: {head[:80]!r}")
    fields = head.split(b" ")
    if fields[-1] in (b"missing", b"ambiguous"):
        return None, None, rest
    if len(fields) != 3 or not fields[2].isdigit():
        raise call.Unreadable(answer.name, f"not '<id> <kind> <size>': {head[:80]!r}")
    size = int(fields[2])
    if len(rest) < size + 1 or rest[size:size + 1] != b"\n":
        raise call.Unreadable(answer.name, f"an object of {size} bytes is cut short")
    return fields[1].decode("ascii", "replace"), rest[:size], rest[size + 1:]
