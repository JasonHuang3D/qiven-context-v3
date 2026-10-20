"""The content rule: no file of the checkout but those .gitignore (and the
checkout's own exclude file) names holds a path of this machine, a hit of
the secret check's nine patterns, or a name of the specification's own
repository: its name, its directories' and its files'.

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
# The specification's own repository: its name, its directories, its files'
# stems, each held here in pieces, so that this file holds none, and joined
# by private_names() when the test runs.
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
                found.append((rel, "a name of the specification's repository"))
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
            "repository.md": "the " + "qiven" + "-DOCS repository\n",
            "format.md": "see " + "FORMAT-" + "CODE.md\n",
            "reading.txt": "CORE-" + "READING-3 F183\n",
            "reading.cpp": "// CORE-" + "READING\n",
            "capitals.md": "COGNI" + "TION §12\n",
            "law.md": "the " + "Q" + "LAW.md text\n",
            "ruling.py": "x = '" + "Cognition" + "-Format.md'\n",
            "directory.txt": "rulings" + "/a.md\n",
            "context.cmake": "# proposal" + "/context" + "-v3/\n",
            "word.md": "AR" + "CH.md\n",
            "key.txt": "-----BEGIN " + "RSA PRIVATE KEY-----\n",
            "token.py": "t = '" + "ghp_" + "A" * 36 + "'\n",
            "password.ini": "Server=db;Password=" + "p" * 8 + ";\n",
            "clean.txt": f"{none}:\\no-such\\x /{none.lower()}/x C:/Users/ABC docs/FORMAT.md "
                         + "ghp_" + "A" * 35 + " password: see the vault " + "https://example.com:8080/a@b\n"
                         + "README.md cognition, a challenge, ARCH, QTest, a law firm, work-line, the "
                         + "qiven-context-v3/ tree and context-v3, FORMAT-ted, xFORMAT-A\n",
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
        names = {"repository.md", "format.md", "reading.txt", "reading.cpp", "capitals.md", "law.md", "ruling.py",
                 "directory.txt", "context.cmake", "word.md"}
        self.assertEqual(files, {"checkout.txt", "slash.py", "drive.cpp", "posix.md", "profile.txt",
                                 "interpreter.json", "key.txt", "token.py", "password.ini"} | names)
        self.assertEqual({rel for rel, why in found if why == "a name of the specification's repository"}, names)
        self.assertIn(("drive.cpp", "a drive path whose first directory this machine has"), found)
        self.assertIn(("posix.md", "a drive path whose first directory this machine has"), found)
        hits = {why for _, why in found if why.startswith("a hit of ")}
        self.assertEqual(hits, {"a hit of private-key", "a hit of github-ghp", "a hit of pair-password"})
