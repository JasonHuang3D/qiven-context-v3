"""The tree's files, as rules.py's checker reads them, through git
(foundation/devkit/python/git.py) under its reading environment: every file
git tracks or would add, as the working tree holds it, and which of them git
treats as text (B32). It holds no rule: rules.py's rules read what it gives.
"""

import tempfile

from foundation.devkit.python import git


def _read(call):
    """call(env), git's reading environment given, in a scratch directory of its own."""
    with tempfile.TemporaryDirectory() as scratch:
        return call(git.reading_env(scratch))


def source_files(root):
    """Every file git tracks under root or would add, its ignore files read,
    that the working tree holds."""
    names = _read(lambda env: git.tracked(root, others=True, env=env))
    return sorted(name for name in set(names) if (root / name).is_file())


def text_files(root, files):
    """The files of files that git treats as text: all but those whose text
    attribute is unset (-text, binary)."""
    read = _read(lambda env: git.attributes(root, files, ("text",), env=env)) if files else {}
    return {rel for rel, values in read.items() if values["text"] != "unset"}
