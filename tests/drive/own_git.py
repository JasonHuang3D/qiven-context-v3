"""A git environment of a case's own: the case's git, and the git the code
under test runs, read no inherited GIT_ variable, no system or global
config and no system attributes file, and set in git's command scope,
which no config file overrides, each setting a host's config could turn
against a case: core.autocrlf false, hooks from an empty directory, no
signing, the initial branch, and the author and committer named; so that
a repository's commits and checkouts hold the same bytes, and its commands
succeed, on every host, whatever the host's git config says (CI's Windows
images set core.autocrlf=true in the system config)."""

import os
import shutil
import tempfile
from pathlib import Path
from unittest import mock

WHO = {"GIT_AUTHOR_NAME": "case", "GIT_AUTHOR_EMAIL": "case@localhost",
       "GIT_COMMITTER_NAME": "case", "GIT_COMMITTER_EMAIL": "case@localhost"}


def git_env(top):
    """The inherited environment less every GIT_ variable, git's system
    config and attributes off, its global config an empty file under top,
    a scratch directory, hooks from an empty directory under it, and the
    command scope's settings."""
    top = Path(top)
    empty, hooks = top / "gitconfig", top / "githooks"
    empty.write_bytes(b"")
    hooks.mkdir(exist_ok=True)
    config = {"core.autocrlf": "false", "core.safecrlf": "false", "core.hooksPath": hooks.as_posix(),
              "commit.gpgsign": "false", "tag.gpgsign": "false", "init.defaultBranch": "main"}
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(WHO, GIT_CONFIG_NOSYSTEM="1", GIT_ATTR_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty),
               GIT_CONFIG_COUNT=str(len(config)))
    for i, (key, value) in enumerate(config.items()):
        env[f"GIT_CONFIG_KEY_{i}"], env[f"GIT_CONFIG_VALUE_{i}"] = key, value
    return env


def own_git(case, top=None):
    """os.environ for the case's length: git_env(top), top by default a
    directory of its own, removed after the case, so that no file of the
    git environment lands in a scratch checkout."""
    if top is None:
        top = tempfile.mkdtemp(prefix="qiven3-own-git-")
        case.addCleanup(shutil.rmtree, top, ignore_errors=True)
    patcher = mock.patch.dict(os.environ, git_env(top), clear=True)
    patcher.start()
    case.addCleanup(patcher.stop)
