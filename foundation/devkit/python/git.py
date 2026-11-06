"""Git, for the repository's Python: the one place a program starts git,
through process, with its argv and its decoding. This directory's README states
each member's call on each system.

find() finds git. tracked() and attributes() read a checkout, as git finds its
repository from there; full_hash() and files_at() read root's own .git.
reading_env() is the environment under which git reads the same whatever the
host's git config says; tracked() and attributes() take it as env.

Every failure of git is GitFailed, in one line: git missing, a start that
failed, a limit passed, a status other than 0, an answer that is no UTF-8.
"""

import os
from pathlib import Path

from foundation.devkit.python import process

_LIMIT = 300  # seconds a run of git takes
_SAID = 500  # characters kept of the end of what git said on a failure


class GitFailed(Exception):
    """Git did not answer: one line, naming its command and the reason."""


def find():
    """PATH's git, as a str, or None."""
    return process.find("git")


def tracked(root, others=False, env=None):
    """The names of the files git tracks under root, each from root, in git's
    order; with others, the untracked ones no ignore file names too."""
    argv = ["ls-files", "-z", *(("-co", "--exclude-standard") if others else ())]
    return [name for name in _read(argv, cwd=root, env=env).split("\0") if name]


def attributes(root, paths, names, env=None):
    """{path: {name: value}} of the attributes `names` of `paths`, each from
    root, as git reads them: a value is git's own word, "set", "unset",
    "unspecified" or what the attribute was given."""
    data = "".join(f"{path}\0" for path in paths).encode("utf-8")
    fields = _read(["check-attr", "-z", "--stdin", *names], cwd=root, env=env, data=data).split("\0")
    read = {}
    for path, name, value in zip(fields[::3], fields[1::3], fields[2::3]):
        read.setdefault(path, {})[name] = value
    return read


def full_hash(root, commit):
    """The full hash of `commit`, whatever form names it, in root's .git."""
    return _read([*_at(root), "rev-parse", "--verify", f"{commit}^{{commit}}"]).strip()


def files_at(root, commit, wanted):
    """{path: its text, UTF-8} of each file at `commit` in root's .git whose
    path `wanted` answers true of. Two runs of git."""
    listed = _read([*_at(root), "ls-tree", "-r", "-z", "--name-only", f"{commit}^{{commit}}"])
    paths = [path for path in listed.split("\0") if path and wanted(path)]
    argv = [*_at(root), "cat-file", "--batch"]
    rest = _run(argv, data="".join(f"{commit}:{path}\n" for path in paths).encode("utf-8"))
    files = {}
    for path in paths:  # each "<id> blob <size>", its bytes, a newline
        head, _, rest = rest.partition(b"\n")
        fields = head.split()
        if fields[1:2] != [b"blob"]:
            raise GitFailed(f"{_named(argv)}: no file at {path}: {head.decode('utf-8', 'replace')}")
        size = int(fields[2])
        files[path], rest = _text(rest[:size], argv), rest[size + 1:]
    return files


def reading_env(scratch):
    """Git's environment for reading a checkout's tracked files and attributes
    so that it reads the same whatever the host's git config says: the inherited
    environment less every GIT_ variable, git's system config and attributes
    off, its global config and attributes each an empty file under scratch, a
    directory of the caller's, and, in git's command scope, which no config file
    overrides, the same attributes file and no fsmonitor."""
    empty = Path(scratch) / "gitconfig"
    empty.write_bytes(b"")
    config = {"core.attributesFile": empty.as_posix(), "core.fsmonitor": "false"}
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_ATTR_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty),
               GIT_CONFIG_COUNT=str(len(config)))
    for i, (key, value) in enumerate(config.items()):
        env[f"GIT_CONFIG_KEY_{i}"], env[f"GIT_CONFIG_VALUE_{i}"] = key, value
    return env


def _at(root):
    """The words that make git read root's own .git, and no parent's."""
    return ["--git-dir", str(Path(root) / ".git")]


def _named(argv):
    """A run of git, as a failure names it."""
    return " ".join(("git", *argv))


def _run(argv, cwd=None, env=None, data=None):
    """Git's output, bytes, from a run with argv; GitFailed where there is none."""
    program = find()
    if program is None:
        raise GitFailed("git: none on PATH")
    try:
        done = process.run([program, *argv], data=data, env=env, cwd=cwd, limit=_LIMIT)
    except (OSError, process.TimedOut) as error:
        raise GitFailed(f"{_named(argv)}: {error}") from error
    if done.status != 0:
        said = " ".join(done.err.decode("utf-8", "replace").split())
        raise GitFailed(f"{_named(argv)}: {done.named}: {said[-_SAID:]}")
    return done.out


def _read(argv, cwd=None, env=None, data=None):
    """Git's output, as text, from a run with argv."""
    return _text(_run(argv, cwd, env, data), argv)


def _text(data, argv):
    """data, as text; GitFailed naming the run where it is no UTF-8."""
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise GitFailed(f"{_named(argv)}: its answer is no UTF-8: {error}") from error
