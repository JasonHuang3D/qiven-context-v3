"""The content rule: no file of the checkout but those git's ignore files
name (tree.py's) holds a path of this machine, a hit of
the secret check's twelve patterns, or a name of the specification's own
repository: its name, its directories' and its files', or a reference into
its text by a ruling's or a section's number (REFERENCE); nor a name of
this machine, its user's or its own, where it stands as a component of a
path. content_findings() applies it to a set of files; checks.py, to each
module's files of v3's tree.

The paths are read when the check runs (machine_paths()), so that this
file holds none: the
checkout's own and its parent, the user profile, the interpreter's and the
toolchain's directories, in either slash, escaped or not, and any case. And
any drive-letter path (X:\\dir, X:/dir) or its /x/dir form whose first
directory is a profile's root, Users or Documents and Settings, in any case:
read from the text alone, never from what this machine's drives hold, so it
reads the same on every host. A documented example value a fixture holds is
listed in lists.EXAMPLES with its source, exempt by exact value.

The names are read when the check runs as well (machine_names()): the
user's from USERNAME,
USER, LOGNAME, the profile's last directory and the login, the computer's
from COMPUTERNAME and the host's name. One is found where it stands as a
path's component, in either slash and any case: after a separator or a
home's tilde, and before a separator or the path's end; or at a path's
start before a separator. The same characters inside other text, a key's
hex, a longer word or a number, are not a name (a name under three
characters is not read, a residual)."""

import getpass
import os
import platform
import re
import sys

from foundation.devkit.python import process
from foundation.devkit.structure import layers, lists

# The secret check's twelve patterns, in its table's order, each written as
# the table writes it.
PATTERNS = {
    "private-key": r"-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY(?: BLOCK)?-----",
    "github-ghp": r"(?<![A-Za-z0-9_])ghp_[A-Za-z0-9]{36}",
    "github-pat": r"(?<![A-Za-z0-9_])github_pat_[A-Za-z0-9_]{82}",
    "sk-key": r"(?<![A-Za-z0-9_-])sk-[A-Za-z0-9_-]{32,}",
    "aws-akia": r"(?<![A-Z0-9])AKIA[A-Z0-9]{16}",
    "slack-xox": r"(?<![A-Za-z0-9])xox[abceoprs]-[0-9]+-[A-Za-z0-9-]{8,}",
    "slack-xoxd": r"(?<![A-Za-z0-9])xoxd-[A-Za-z0-9%+/=]{32,}",
    "url-password": r"[A-Za-z][A-Za-z0-9+.-]*://[^ \t\r\n/?#@:]*:[^ \t\r\n/?#@]+@",
    "pair-password": r"""(?<![A-Za-z0-9])(?:PG)?[Pp](?:[Aa][Ss][Ss])?[Ww](?:[Oo][Rr])?[Dd][ \t]*=[ \t]*["']?"""
                     r"""[^ \t\r\n;&'"]+""",
    "auth-header": r"""(?<![A-Za-z0-9_])[Aa][Uu][Tt][Hh][Oo][Rr][Ii][Zz][Aa][Tt][Ii][Oo][Nn]["']?[ \t]*:[ \t]*["']?"""
                   r"(?:[Bb][Ee][Aa][Rr][Ee][Rr]|[Bb][Aa][Ss][Ii][Cc]|[Tt][Oo][Kk][Ee][Nn]|[Aa][Pp][Ii][Kk][Ee][Yy])"
                   r"[ \t]+[A-Za-z0-9._~+/=-]{8,}",
    "cli-secret": r"""(?<![A-Za-z0-9_-])--(?:password|token|api-key|secret)(?:[ \t]+|[ \t]*=[ \t]*)["']?"""
                  r"""[^ \t\r\n"'$<%{-]""",
    "tencent-akid": r"(?<![A-Za-z0-9])AKID[A-Za-z0-9]{32}",
}
COMPILED = {name: re.compile(pattern) for name, pattern in PATTERNS.items()}
# The specification's own repository: its name, its directories, its files'
# stems, each held here in pieces, so that this file holds none, and joined
# by private_names() when the check runs.
REPOSITORY = ("qiven", "docs")
DIRECTORIES = (("rulings",), ("law",), ("proposal",), ("context", "v3"), ("law", "compiler"), ("test", "runner"),
               ("work", "line"))
# The stems in capitals, each a name with .md or without; the format files'
# and the readings' are families, a word or a number after the stem.
CAPITALS = (("cognition",), ("challenge",), ("core", "challenge"), ("cxx", "survey"), ("prior", "art"),
            ("oplaw",), ("qidx",), ("qrev",), ("qlaw",), ("qtest",))
FAMILIES = ((("format",), "-[A-Z]+"), (("core", "reading"), "(?:-[0-9]+)?"))
# The rulings' stems, in small letters, with .md or without, in any case.
RULINGS = (("cognition", "format"), ("cold", "start", "gaps"), ("consent", "round", "1"), ("context", "v3", "start"),
           ("contract", "03", "discharge"), ("docs", "restructure"), ("docs", "restructure", "discharge"),
           ("law", "landing", "discharge"), ("retrieval", "line"), ("work", "line", "defects", "discharge"))
# Stems that are words, refused with .md alone; the readme and the format
# document, which the public tree holds as well, are not refused.
WORDS = ("arch", "cont", "eng", "work")
BEFORE, AFTER = r"(?<![A-Za-z0-9_-])", r"(?![A-Za-z0-9_])"
# A reference into the specification's text by its numbers, in any case but
# the finding's and the review's forms: the word ruling or rulings before a
# number, space, #, -, _, : or a section sign between; a number of two digits
# or a dotted one before the word ruling; the word section before a number of
# two digits, a dotted one or one and its item; sec. before a number; an item
# of a dotted number, or a dotted number and its item; a section sign, its
# entity too, before a number, after any character; the words challenge,
# anomaly and brief before a number; a review's finding (two digits, a
# capital A to D and a number) and a finding's number (a capital F and a
# number of one to three digits), each a token of its own, but the linter's
# code a noqa: names first. A ruling as a word ("a ruling", an object type),
# a step's, a part's or an undotted section's single number, a version and a
# hex token are none. The vendored files, not v3's text, are not read for it
# (sqlite3.c cites a standard's numbered section).
_TOKEN, _END = r"(?<![A-Za-z0-9_])", r"(?![A-Za-z0-9_])"
REFERENCE = re.compile(
    _TOKEN + r"(?i:rulings?[\s#_:\xa7-]*\d"
    r"|(?:\d{2,}|\d+\.\d+)[\s-]+rulings?" + _END
    + r"|sections?\s+#?(?:\d{2}|\d+\.\d|\d+,?\s+items?\s+\d)"
    r"|secs?\.\s*\d"
    r"|items?\s+\d+\s+(?:of|in)\s+\d+\.\d|\d+\.\d+,?\s+items?\s+\d"
    r"|(?:challenges?|anomal(?:y|ies)|briefs?)\s+#?\d)"
    r"|(?i:\xa7|&sect;)\s*\d"
    r"|" + _TOKEN + r"(?:\d{2}[A-D]\d{1,2}|(?<!noqa: )F\d{1,3})" + _END)
REFERENCE_FINDING = "a ruling's or section's number"
VENDORED = layers.VENDORED  # tools/trees.json's vendored: the vendored files, not their module's


def private_names():
    """The patterns of the specification repository's names."""
    def name(parts):
        return re.escape("-".join(parts))

    def either(names):
        return "(?:" + "|".join(names) + ")"

    families = [name(stem).upper() + more for stem, more in FAMILIES]
    return (re.compile(name(REPOSITORY), re.IGNORECASE),
            re.compile(BEFORE + either(name(d) for d in DIRECTORIES) + "/"),
            re.compile(BEFORE + either([name(c).upper() for c in CAPITALS] + families) + AFTER),
            re.compile(BEFORE + either(name(r) for r in RULINGS) + AFTER, re.IGNORECASE),
            re.compile(BEFORE + either(w.upper() for w in WORDS) + r"\.md" + AFTER))


PRIVATE = private_names()
STOP = " \t\r\n\"'`<>|*?;,()[]{}"
DRIVE = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z]):(?:\\|/)+([^\\/" + re.escape(STOP) + r":]+)")
POSIX = re.compile(r"(?<![A-Za-z0-9_.:/~-])/([A-Za-z])/([^\\/" + re.escape(STOP) + r":]+)")
# A profile's root as a drive path's first directory, in small letters:
# Users, and Documents and Settings, which the patterns cut at its space.
PROFILE_ROOTS = frozenset(("users", "documents"))
DRIVE_FINDING = "a drive path into a profile's root"


def machine_paths():
    """This machine's directories, each with two components at least, the
    checkout's parent with one."""
    parent = os.path.normpath(layers.ROOT.parent)
    found = {str(layers.ROOT), parent, sys.prefix, sys.base_prefix, os.path.dirname(sys.executable)}
    for name in ("USERPROFILE", "HOME", "VCToolsInstallDir", "VCINSTALLDIR", "VSINSTALLDIR", "WindowsSdkDir",
                 "UniversalCRTSdkDir", "QIVEN_CTEST"):
        if os.environ.get(name):
            found.add(os.environ[name] if name != "QIVEN_CTEST" else os.path.dirname(os.environ[name]))
    for tool in ("cmake", "ninja", "cl"):
        file = process.find(tool)
        if file:
            found.add(os.path.dirname(file))
    forms = set()
    for path in found:
        path = os.path.normpath(path).rstrip("\\/")
        parts = path.replace("\\", "/").split("/")
        if len(parts) < (2 if path == parent else 3) or not os.path.isabs(path):
            continue
        slash = "/".join(parts)
        forms |= {slash.lower(), slash.replace("/", "\\").lower(), slash.replace("/", "\\\\").lower()}
        if len(parts[0]) == 2 and parts[0][1] == ":":
            forms.add(("/" + parts[0][0] + "/" + "/".join(parts[1:])).lower())
    return sorted(forms)


def machine_names():
    """This machine's names, the user's and the computer's, in small letters."""
    found = set()
    for name in ("USERNAME", "USER", "LOGNAME", "COMPUTERNAME"):
        found.add(os.environ.get(name, ""))
    for name in ("USERPROFILE", "HOME"):
        found.add(os.path.basename(os.path.normpath(os.environ[name])) if os.environ.get(name) else "")
    try:
        found.add(getpass.getuser())
    except OSError:
        pass
    node = platform.node()
    found |= {node, node.split(".")[0]}
    return sorted(name.lower() for name in found if len(name) >= 3)


# A path's separator, either slash; and a path's end: a separator, a stop,
# a list's colon, a sentence's full stop, or the text's end.
SEP = r"[\\/]"
END = r"(?=[\\/" + re.escape(STOP) + r":]|\.(?:[" + re.escape(STOP) + r"]|$)|$)"


def name_pattern(names):
    """A name of this machine as a path's component, or None for no name."""
    if not names:
        return None
    name = "(?:" + "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True)) + ")"
    after = r"(?<=[\\/~])" + name + END
    first = r"(?<![^" + re.escape(STOP) + r"])" + name + "(?=" + SEP + ")"
    return re.compile(after + "|" + first, re.IGNORECASE)


def token_at(text, start):
    end = start
    while end < len(text) and text[end] not in STOP:
        end += 1
    return text[start:end].replace("\\\\", "\\")


def profile_root(first):
    return first.lower() in PROFILE_ROOTS


def content_findings(root, files, paths, names=()):
    found = []
    named = name_pattern(names)
    for rel in files:
        text = (root / rel).read_bytes().decode("utf-8", "replace")
        lower = text.lower()
        for path in paths:
            if path in lower:
                found.append((rel, "a path of this machine"))
        for match in list(DRIVE.finditer(text)) + list(POSIX.finditer(text)):
            if profile_root(match.group(2)) and token_at(text, match.start()) not in lists.EXAMPLES:
                found.append((rel, DRIVE_FINDING))
        if named and named.search(text):
            found.append((rel, "a name of this machine"))
        for name, pattern in COMPILED.items():
            if pattern.search(text):
                found.append((rel, "a hit of " + name))
        for pattern in PRIVATE:
            if pattern.search(text):
                found.append((rel, "a name of the specification's repository"))
        if rel not in VENDORED and REFERENCE.search(text):
            found.append((rel, REFERENCE_FINDING))
    return sorted(set(found))
