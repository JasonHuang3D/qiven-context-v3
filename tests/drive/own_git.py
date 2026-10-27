"""A git environment of a case's own: the case's git, and the git the code
under test runs, read no inherited GIT_ variable, no system or global
config and no system or global attributes or excludes file, and set in
git's command scope, which no config file overrides, each setting a
host's config could turn against a case: core.autocrlf false, hooks from
an empty directory, no fsmonitor, no signing, the initial branch, and the
author and committer named; so that a repository's commits and checkouts
hold the same bytes, and its commands succeed, on every host, whatever
the host's git config says (CI's Windows images set core.autocrlf=true in
the system config). The environment is the loop's tools' own,
tools/loop_run.py's git_env, with the case's identity."""

import os
import shutil
import tempfile
from unittest import mock

from tools.loop_run import git_env as tools_git_env

WHO = ("case", "case@localhost")


def git_env(top):
    """tools/loop_run.py's git_env under top, a scratch directory, the
    author and committer the case's."""
    return tools_git_env(top, *WHO)


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
