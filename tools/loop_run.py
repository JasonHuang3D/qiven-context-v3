"""git's own environment, git_env(), each case's (tests/drive/own_git.py).

Loaded by runpy.run_path or imported: it imports nothing of this
directory.
"""

import os
from pathlib import Path

# git_env's settings against git's background work: no automatic maintenance
# after a command, no automatic gc, and either in the foreground if run.
QUIET = {"maintenance.auto": "false", "maintenance.autoDetach": "false", "gc.auto": "0", "gc.autoDetach": "false"}


def git_env(top, name, email):
    """git's own environment, each case's (tests/drive/own_git.py), so
    that git computes the same and succeeds whatever the host's git config
    says: the inherited environment less every GIT_ variable, git's system
    config and attributes off, its global config, excludes and attributes
    each an empty file under top, a scratch directory, hooks from an empty
    directory under it, and, in git's command scope, which no config file
    overrides, each setting a host's config could turn against it:
    core.autocrlf false (the checkout's .gitattributes governs line
    endings), no fsmonitor, no signing, the initial branch, and the author
    and committer named; and no background work, so that no git command
    leaves a process writing in a repository after it returns (a newer git
    detaches the maintenance a commit or a fetch starts, an older one gc
    --auto): no automatic maintenance or gc, and either in the foreground
    should one run."""
    top = Path(top)
    empty, hooks = top / "gitconfig", top / "githooks"
    empty.write_bytes(b"")
    hooks.mkdir(exist_ok=True)
    config = {"core.autocrlf": "false", "core.safecrlf": "false", "core.hooksPath": hooks.as_posix(),
              "core.excludesFile": empty.as_posix(), "core.attributesFile": empty.as_posix(),
              "core.fsmonitor": "false", "commit.gpgsign": "false", "tag.gpgsign": "false",
              "init.defaultBranch": "main", **QUIET}
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(GIT_AUTHOR_NAME=name, GIT_AUTHOR_EMAIL=email, GIT_COMMITTER_NAME=name, GIT_COMMITTER_EMAIL=email,
               GIT_CONFIG_NOSYSTEM="1", GIT_ATTR_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty),
               GIT_CONFIG_COUNT=str(len(config)))
    for i, (key, value) in enumerate(config.items()):
        env[f"GIT_CONFIG_KEY_{i}"], env[f"GIT_CONFIG_VALUE_{i}"] = key, value
    return env
