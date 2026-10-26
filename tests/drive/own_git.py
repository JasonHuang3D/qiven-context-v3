"""A git environment of a case's own: the case's git, and the git the code
under test runs, read no inherited GIT_ variable and no system or global
config, and core.autocrlf is false by the environment, so that a scratch
repository's commits and checkouts hold the same bytes on every host,
whatever the host's git config says (CI's Windows images set
core.autocrlf=true in the system config)."""

import os
from pathlib import Path
from unittest import mock

WHO = {"GIT_AUTHOR_NAME": "case", "GIT_AUTHOR_EMAIL": "case@localhost",
       "GIT_COMMITTER_NAME": "case", "GIT_COMMITTER_EMAIL": "case@localhost"}


def own_git(case, top):
    """os.environ for the case's length: the inherited environment less every
    GIT_ variable, git's system config off, its global config an empty file
    under top, the case's scratch directory, core.autocrlf false in git's
    command scope, which no config file overrides, and the author and
    committer named."""
    empty = Path(top) / "gitconfig"
    empty.write_bytes(b"")
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(WHO, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty), GIT_CONFIG_COUNT="1",
               GIT_CONFIG_KEY_0="core.autocrlf", GIT_CONFIG_VALUE_0="false")
    patcher = mock.patch.dict(os.environ, env, clear=True)
    patcher.start()
    case.addCleanup(patcher.stop)
