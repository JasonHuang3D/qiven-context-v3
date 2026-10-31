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
tools/loop_run.py's git_env, with the case's identity.

Every git a case starts, its fixture's or the tools', starts by git's one
door (tools/loop_git.py), which counts it; a case whose property is the
tool's, not git's, takes the fake (fake=True, tests/drive/fake_git.py),
which starts none."""

import os
import shutil
import tempfile
from pathlib import Path
from unittest import mock

from tools import loop_git
from tools.loop_run import git_env as tools_git_env

WHO = ("case", "case@localhost")
FAKE = str(Path(__file__).resolve().parent / "fake_git.py")


def git_env(top):
    """tools/loop_run.py's git_env under top, a scratch directory, the
    author and committer the case's."""
    return tools_git_env(top, *WHO)


def own_git(case, top=None, fake=False):
    """os.environ for the case's length: git_env(top), top by default a
    directory of its own, removed after the case, so that no file of the
    git environment lands in a scratch checkout; with fake, the fake git
    in git's place for the case and every process it starts."""
    if top is None:
        top = tempfile.mkdtemp(prefix="qiven3-own-git-")
        case.addCleanup(shutil.rmtree, top, ignore_errors=True)
    env = git_env(top)
    env.pop(loop_git.FAKE, None)
    if fake:
        env[loop_git.FAKE] = FAKE
    patcher = mock.patch.dict(os.environ, env, clear=True)
    patcher.start()
    case.addCleanup(patcher.stop)


def git(root, *argv, check=True):
    """git's output in root, the door's git (the fake under
    own_git(fake=True)), under os.environ; CalledProcessError on a failure
    when check."""
    return loop_git.run(loop_git.which(), argv, cwd=root, check=check).stdout.decode("utf-8").strip()
