"""git's own environment, git_env(), a case's (foundation/devkit/structure/tests/test_line_endings.py).

Imported: it imports nothing of this directory.
"""

import os
from pathlib import Path


def git_env(top):
    """git's own environment, for its reading of a checkout's tracked files
    and attributes (ls-files, check-attr), so that git reads the same whatever
    the host's git config says: the inherited environment less every GIT_
    variable, git's system config and attributes off, its global config and
    attributes each an empty file under top, a scratch directory, and, in
    git's command scope, which no config file overrides, the same attributes
    file and no fsmonitor."""
    empty = Path(top) / "gitconfig"
    empty.write_bytes(b"")
    config = {"core.attributesFile": empty.as_posix(), "core.fsmonitor": "false"}
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_ATTR_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty),
               GIT_CONFIG_COUNT=str(len(config)))
    for i, (key, value) in enumerate(config.items()):
        env[f"GIT_CONFIG_KEY_{i}"], env[f"GIT_CONFIG_VALUE_{i}"] = key, value
    return env
