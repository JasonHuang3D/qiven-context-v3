"""The content rule: no file of the checkout but those .gitignore (and the
checkout's own exclude file) names holds a path of this machine, a hit of
the secret check's nine patterns, or the specification's private names.

The paths are read when the test runs, so that it holds none: the
checkout's own, the user profile, the interpreter's and the toolchain's
directories, in either slash, escaped or not, and any case; and any
drive-letter path (X:\\dir, X:/dir) or its /x/dir form whose first
directory this machine has. A documented example value a fixture holds is
listed in rules.EXAMPLES with its source, exempt by exact value."""

import os
import re
import shutil
import string
import sys
import tempfile
import unittest
from pathlib import Path

from tests.structure import rules

# The secret check's nine patterns.
PATTERNS = {
    "private-key": r"-----BEGIN (?:[A-Z0-9]+ )*PRIVATE KEY(?: BLOCK)?-----",
    "github-ghp": r"(?<![A-Za-z0-9_])ghp_[A-Za-z0-9]{36}",
    "github-pat": r"(?<![A-Za-z0-9_])github_pat_[A-Za-z0-9_]{82}",
    "sk-key": r"(?<![A-Za-z0-9_-])sk-[A-Za-z0-9_-]{32,}",
    "aws-akia": r"(?<![A-Z0-9])AKIA[A-Z0-9]{16}",
    "slack-xox": r"(?<![A-Za-z0-9])xox[abceoprs]-[0-9]+-[A-Za-z0-9-]{8,}",
    "slack-xoxd": r"(?<![A-Za-z0-9])xoxd-[A-Za-z0-9%+/=]{32,}",
    "url-password": r"[A-Za-z][A-Za-z0-9+.-]*://[^ \t\r\n/?#@:]*:[^ \t\r\n/?#@]+@",
    "pair-password": r"(?<![A-Za-z0-9])(?:PG)?[Pp](?:[Aa][Ss][Ss])?[Ww](?:[Oo][Rr])?[Dd][ \t]*=[ \t]*[\"']?"
                     r"[^ \t\r\n;&'\"]+",
}
COMPILED = {name: re.compile(pattern) for name, pattern in PATTERNS.items()}
# The specification's own repository and file names, built here so that this
# file holds none.
PRIVATE = (re.compile(re.escape("qiven" + "-docs"), re.IGNORECASE), re.compile(r"\bFORMAT-[A-Z]+\.md\b"))
STOP = " \t\r\n\"'`<>|*?;,()[]{}"
DRIVE = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z]):(?:\\|/)+([^\\/" + re.escape(STOP) + r":]+)")
POSIX = re.compile(r"(?<![A-Za-z0-9_.:/~-])/([A-Za-z])/([^\\/" + re.escape(STOP) + r":]+)")


def machine_paths():
    """This machine's directories, each with two components at least."""
    found = {str(rules.ROOT), sys.prefix, sys.base_prefix, os.path.dirname(sys.executable)}
    for name in ("USERPROFILE", "HOME", "VCToolsInstallDir", "VCINSTALLDIR", "VSINSTALLDIR", "WindowsSdkDir",
                 "UniversalCRTSdkDir", "QIVEN3_CTEST"):
        if os.environ.get(name):
            found.add(os.environ[name] if name != "QIVEN3_CTEST" else os.path.dirname(os.environ[name]))
    for tool in ("cmake", "ninja", "cl"):
        if shutil.which(tool):
            found.add(os.path.dirname(shutil.which(tool)))
    forms = set()
    for path in found:
        path = os.path.normpath(path).rstrip("\\/")
        parts = path.replace("\\", "/").split("/")
        if len(parts) < 3 or not os.path.isabs(path):
            continue
        slash = "/".join(parts)
        forms |= {slash.lower(), slash.replace("/", "\\").lower(), slash.replace("/", "\\\\").lower()}
        if len(parts[0]) == 2 and parts[0][1] == ":":
            forms.add(("/" + parts[0][0] + "/" + "/".join(parts[1:])).lower())
    return sorted(forms)


def token_at(text, start):
    end = start
    while end < len(text) and text[end] not in STOP:
        end += 1
    return text[start:end].replace("\\\\", "\\")


def exists(letter, first):
    return os.path.isdir(f"{letter}:\\{first}")


def content_findings(root, files, paths):
    found = []
    for rel in files:
        text = (root / rel).read_bytes().decode("utf-8", "replace")
        lower = text.lower()
        for path in paths:
            if path in lower:
                found.append((rel, "a path of this machine"))
        for match in list(DRIVE.finditer(text)) + list(POSIX.finditer(text)):
            if exists(match.group(1), match.group(2)) and token_at(text, match.start()) not in rules.EXAMPLES:
                found.append((rel, "a drive path whose first directory this machine has"))
        for name, pattern in COMPILED.items():
            if pattern.search(text):
                found.append((rel, "a hit of " + name))
        for pattern in PRIVATE:
            if pattern.search(text):
                found.append((rel, "the specification's private name"))
    return sorted(set(found))


def absent_drive():
    return next(c for c in string.ascii_uppercase[::-1] if not os.path.exists(c + ":\\"))


@unittest.skipUnless(sys.platform == "win32", "the content rule reads this machine's paths, on Windows")
class Content(unittest.TestCase):
    def test_tree(self):
        self.assertEqual(content_findings(rules.ROOT, rules.source_files(rules.ROOT), machine_paths()), [])

    def test_planted(self):
        drive, first = rules.ROOT.drive[0], rules.ROOT.parts[1]
        none = absent_drive()
        plants = {
            "checkout.txt": str(rules.ROOT) + "\\x",
            "slash.py": "p = '" + rules.ROOT.as_posix().upper() + "/y'\n",
            "drive.cpp": f"// {drive}:\\{first}-not-the-checkout\\x and {drive}:/{first}/y\n",
            "posix.md": f"cd /{drive.lower()}/{first}\n",
            "profile.txt": os.environ["USERPROFILE"] + "\\Documents\n",
            "interpreter.json": '{"python": "' + sys.base_prefix.replace("\\", "\\\\") + '"}\n',
            "private.md": "the " + "qiven" + "-docs repository and " + "FORMAT-" + "CODE.md\n",
            "key.txt": "-----BEGIN " + "RSA PRIVATE KEY-----\n",
            "token.py": "t = '" + "ghp_" + "A" * 36 + "'\n",
            "password.ini": "Server=db;Password=" + "p" * 8 + ";\n",
            "clean.txt": f"{none}:\\no-such\\x /{none.lower()}/x C:/Users/ABC docs/FORMAT.md "
                         + "ghp_" + "A" * 35 + " password: see the vault " + "https://example.com:8080/a@b\n",
            "build/machine.txt": str(rules.ROOT),
        }
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / ".gitignore").write_text("build/\n", encoding="utf-8")
            for name, text in plants.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_bytes(text.encode("utf-8"))
            found = content_findings(root, rules.source_files(root), machine_paths())
        files = {rel for rel, _ in found}
        self.assertEqual(files, {"checkout.txt", "slash.py", "drive.cpp", "posix.md", "profile.txt",
                                 "interpreter.json", "private.md", "key.txt", "token.py", "password.ini"})
        self.assertIn(("drive.cpp", "a drive path whose first directory this machine has"), found)
        self.assertIn(("posix.md", "a drive path whose first directory this machine has"), found)
        hits = {why for _, why in found if why.startswith("a hit of ")}
        self.assertEqual(hits, {"a hit of private-key", "a hit of github-ghp", "a hit of pair-password"})
