"""A git environment of a case's own: the git a case starts reads no
inherited GIT_ variable, no system or global config and no system or
global attributes or excludes file, and is set in git's command scope,
which no config file overrides, each setting a host's config could turn
against a case: core.autocrlf false, hooks from an empty directory, no
fsmonitor, no signing, the initial branch, and the author and committer
named; so that its commands succeed, on every host, whatever the host's
git config says (CI's Windows images set core.autocrlf=true in the system
config). The environment is tools/loop_run.py's git_env, with the case's
identity; the case starts git by git's one door (tools/loop_git.py)."""

from tools.loop_run import git_env as tools_git_env

WHO = ("case", "case@localhost")


def git_env(top):
    """tools/loop_run.py's git_env under top, a scratch directory, the
    author and committer the case's."""
    return tools_git_env(top, *WHO)
