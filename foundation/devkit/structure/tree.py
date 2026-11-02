"""The tree's files, as rules.py's checker reads them: every file of the
tree but .git and what the tree's ignore files name, each file's line count
(B15), and whether .gitattributes treats a file as text (B32). It holds no
rule: rules.py's rules read what it gives.
"""

import os
from fnmatch import fnmatchcase
from pathlib import Path


def ignored(rel, name, is_dir, patterns):
    for pattern in patterns:
        if pattern.endswith("/") and not is_dir:
            continue
        pattern = pattern.rstrip("/")
        if fnmatchcase(rel, pattern.lstrip("/")) if "/" in pattern else fnmatchcase(name, pattern):
            return True
    return False


def source_files(root):
    """Every file of the tree but .git and what .gitignore and the checkout's
    own exclude file name."""
    patterns = []
    for name in (".gitignore", ".git/info/exclude"):
        if (root / name).is_file():
            patterns += [line.strip() for line in (root / name).read_text(encoding="utf-8").splitlines()
                         if line.strip() and not line.startswith(("#", "!"))]
    found = []
    for directory, dirs, files in os.walk(root):
        base = Path(directory).relative_to(root).as_posix()
        prefix = "" if base == "." else base + "/"
        dirs[:] = sorted(d for d in dirs if d != ".git" and not ignored(prefix + d, d, True, patterns))
        found += [prefix + f for f in files if not ignored(prefix + f, f, False, patterns)]
    return sorted(found)


def line_count(data):
    return data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)


def text_attributes(root):
    """The lines of the root's .gitattributes that set the text attribute,
    in order: [(pattern, text)]. -text and binary unset it; text, text=...
    and !text leave the file text to B32, git's own content check aside."""
    path = root / ".gitattributes"
    lines = [x.split() for x in path.read_text(encoding="utf-8").splitlines()] if path.is_file() else []
    return [(f[0], a not in ("-text", "binary")) for f in lines if f and not f[0].startswith("#") for a in f[1:]
            if a in ("-text", "binary", "text", "!text") or a.startswith("text=")]


def is_text(rel, attributes):
    """Whether .gitattributes treats rel as text: the last line matching it
    decides, a pattern with a slash matched against the path from the root,
    one without against the name; fnmatch's * crosses a slash, as ** does."""
    matched = [text for pattern, text in attributes
               if fnmatchcase(rel if "/" in pattern else rel.rsplit("/", 1)[-1], pattern.lstrip("/"))]
    return matched[-1] if matched else True
