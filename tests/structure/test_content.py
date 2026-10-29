"""The content rule: no file of the checkout but those .gitignore (and the
checkout's own exclude file) names holds a path of this machine, a hit of
the secret check's twelve patterns, or a name of the specification's own
repository: its name, its directories' and its files', or a reference into
its text by a ruling's or a section's number (REFERENCE); nor a name of
this machine, its user's or its own, where it stands as a component of a
path.

The paths are read when the test runs, so that it holds none: the
checkout's own and its parent, the user profile, the interpreter's and the
toolchain's directories, in either slash, escaped or not, and any case. And
any drive-letter path (X:\\dir, X:/dir) or its /x/dir form whose first
directory is a profile's root, Users or Documents and Settings, in any case:
read from the text alone, never from what this machine's drives hold, so it
reads the same on every host. A documented example value a fixture holds is
listed in rules.EXAMPLES with its source, exempt by exact value.

The names are read when the test runs as well: the user's from USERNAME,
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
import shutil
import string
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.structure import rules

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
# (sqlite3.c cites a standard's numbered section). The plants are written in
# pieces.
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
VENDORED_TREE = "third_party/"


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
# The vendored SQLite's drive paths, its documentation's examples: no
# profile's root, so outside the rule on every host.
VENDORED_DRIVES = "c:/temp C:/lib/mathfuncs.dll C:/Documents%20and%20Settings/fred/Desktop/data.db"


def machine_paths():
    """This machine's directories, each with two components at least, the
    checkout's parent with one."""
    parent = os.path.normpath(rules.ROOT.parent)
    found = {str(rules.ROOT), parent, sys.prefix, sys.base_prefix, os.path.dirname(sys.executable)}
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
            if profile_root(match.group(2)) and token_at(text, match.start()) not in rules.EXAMPLES:
                found.append((rel, DRIVE_FINDING))
        if named and named.search(text):
            found.append((rel, "a name of this machine"))
        for name, pattern in COMPILED.items():
            if pattern.search(text):
                found.append((rel, "a hit of " + name))
        for pattern in PRIVATE:
            if pattern.search(text):
                found.append((rel, "a name of the specification's repository"))
        if not rel.startswith(VENDORED_TREE) and REFERENCE.search(text):
            found.append((rel, REFERENCE_FINDING))
    return sorted(set(found))


def absent_drive():
    return next(c for c in string.ascii_uppercase[::-1] if not os.path.exists(c + ":\\"))


@unittest.skipUnless(sys.platform == "win32", "the content rule reads this machine's paths, on Windows")
class Content(unittest.TestCase):
    ctest_reads = (".",)

    def test_tree(self):
        self.assertEqual(content_findings(rules.ROOT, rules.source_files(rules.ROOT), machine_paths(),
                                          machine_names()), [])

    def test_planted(self):
        drive = rules.ROOT.drive[0]
        none = absent_drive()
        plants = {
            "checkout.txt": str(rules.ROOT) + "\\x",
            "slash.py": "p = '" + rules.ROOT.as_posix().upper() + "/y'\n",
            "parent.md": "see " + rules.ROOT.parent.as_posix().lower() + "/elsewhere/x\n",
            "drive.cpp": f"// {none}:\\Users\\someone\\x and {none.lower()}:/USERS/y\n",
            "posix.md": f"cd /{none.lower()}/users/someone\n",
            "settings.txt": f"{drive}:\\\\Documents and Settings\\\\someone\n",
            "profile.txt": os.environ["USERPROFILE"] + "\\Documents\n",
            "interpreter.json": '{"python": "' + sys.base_prefix.replace("\\", "\\\\") + '"}\n',
            "repository.md": "the " + "qiven" + "-DOCS repository\n",
            "format.md": "see " + "FORMAT-" + "CODE.md\n",
            "reading.txt": "CORE-" + "READING-3 F" + "183\n",
            "reading.cpp": "// CORE-" + "READING\n",
            "capitals.md": "COGNI" + "TION \xa712\n",
            "ruling.md": "by the rul" + "ing 43." + "3 item 3\n",
            "ruling-sign.py": "# rul" + "ings \xa7" + "43\n",
            "section.txt": "see sec" + "tion 43." + "3 item 3\n",
            "sign.cpp": "// \xa7" + " 44.4\n",
            "law.md": "the " + "Q" + "LAW.md text\n",
            "ruling.py": "x = '" + "Cognition" + "-Format.md'\n",
            "directory.txt": "rulings" + "/a.md\n",
            "context.cmake": "# proposal" + "/context" + "-v3/\n",
            "word.md": "AR" + "CH.md\n",
            "key.txt": "-----BEGIN " + "RSA PRIVATE KEY-----\n",
            "token.py": "t = '" + "ghp_" + "A" * 36 + "'\n",
            "password.ini": "Server=db;Password=" + "p" * 8 + ";\n",
            "header.txt": "Authorization: Bearer " + "a" * 20 + "\n",
            "command.sh": "mysql --password " + "p" * 8 + "\n",
            "tencent.ini": "SecretId=AKID" + "a1B2" * 8 + "\n",
            "clean.txt": f"{none}:\\no-such\\x /{none.lower()}/x C:/Users/ABC docs/FORMAT.md {VENDORED_DRIVES} "
                         + f"{drive}:/Windows/System32 /{drive.lower()}/Usersx/y "
                         + "ghp_" + "A" * 35 + " password: see the vault " + "https://example.com:8080/a@b\n"
                         + "README.md cognition, a challenge, ARCH, QTest, a law firm, work-line, the "
                         + "qiven-context-v3/ tree and context-v3, FORMAT-ted, xFORMAT-A\n"
                         + "Authorization: Bearer <token>, --password-stdin, --token $GH_TOKEN, "
                         + "AKID" + "a" * 31 + "\n"
                         + "a ruling, rulings, the ruling: a type, steps 1 to 5, part 1, Sections 1 through 9\n",
            "third_party/x/vendored.c": "** sec" + "tion 6.5.2.2 of a standard\n",
            "build/machine.txt": str(rules.ROOT),
        }
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / ".gitignore").write_text("build/\n", encoding="utf-8")
            for name, text in plants.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_bytes(text.encode("utf-8"))
            found = content_findings(root, rules.source_files(root), machine_paths(), machine_names())
        files = {rel for rel, _ in found}
        names = {"repository.md", "format.md", "reading.txt", "reading.cpp", "capitals.md", "law.md", "ruling.py",
                 "directory.txt", "context.cmake", "word.md"}
        references = {"ruling.md", "ruling-sign.py", "section.txt", "sign.cpp"}
        self.assertEqual(files, {"checkout.txt", "slash.py", "parent.md", "drive.cpp", "posix.md", "settings.txt",
                                 "profile.txt", "interpreter.json", "key.txt", "token.py", "password.ini",
                                 "header.txt", "command.sh", "tencent.ini"} | names | references)
        self.assertEqual({rel for rel, why in found if why == "a name of the specification's repository"}, names)
        self.assertEqual({rel for rel, why in found if why == REFERENCE_FINDING},
                         references | {"capitals.md", "reading.txt"})
        self.assertEqual({rel for rel, why in found if why == DRIVE_FINDING} - {"profile.txt"},
                         {"drive.cpp", "posix.md", "settings.txt"})
        self.assertIn(("parent.md", "a path of this machine"), found)
        hits = {why for _, why in found if why.startswith("a hit of ")}
        self.assertEqual(hits, {"a hit of private-key", "a hit of github-ghp", "a hit of pair-password",
                                "a hit of auth-header", "a hit of cli-secret", "a hit of tencent-akid"})
        self.assertEqual({rel: why for rel, why in found if rel in ("header.txt", "command.sh", "tencent.ini")},
                         {"header.txt": "a hit of auth-header", "command.sh": "a hit of cli-secret",
                          "tencent.ini": "a hit of tencent-akid"})

    def test_drive_host(self):
        """The drive rule reads the text alone: with every directory faked
        present, then absent, the vendored file is clean and a planted
        profile path refused, alike."""
        none = absent_drive()
        outcomes = []
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "plant.txt").write_bytes(f"{none}:\\Users\\someone\\x {VENDORED_DRIVES}\n".encode("utf-8"))
            for present in (True, False):
                with mock.patch("os.path.isdir", return_value=present), \
                        mock.patch("os.path.exists", return_value=present):
                    outcomes.append((content_findings(rules.ROOT, [rules.VENDORED], []),
                                     content_findings(root, ["plant.txt"], [])))
        self.assertEqual(outcomes, [([], [("plant.txt", DRIVE_FINDING)])] * 2)

    def test_planted_names(self):
        """Each plant in a fresh scratch tree: a name of this machine as a
        path's component is found; the same characters inside other text are
        not."""
        names = machine_names()
        user = os.path.basename(os.path.normpath(os.environ["USERPROFILE"]))
        host = platform.node().split(".")[0]
        drive = os.environ["USERPROFILE"][:2]
        self.assertIn(user.lower(), names)
        found = {
            "windows profile": "p = '" + drive + r"\\Users\\" + user + r"\\AppData'" + "\n",
            "windows profile, slash": "see " + drive.lower() + "/users/" + user.upper() + "\n",
            "windows profile, its end": "cd " + drive + r"\Users" + "\\" + user + ".\n",
            "posix home": "export HOME=/home/" + user + ":/opt\n",
            "posix home, tilde": "ls ~" + user + "/x\n",
            "bare component": "out/" + user + "/log.txt\n",
            "bare component, first": user + "/notes.md\n",
        }
        if len(host) >= 3:
            found["computer, a share"] = r"\\" + host.lower() + r"\share" + "\n"
        clean = {
            "hex": '{"key":"e3b0' + user + 'c442"}\n',
            "hex in a path": "/keys/e3b0" + user + "c442/x\n",
            "longer word": "the " + "pre" + user + "post word\n",
            "longer component": drive + "/Users/" + user + "x/y and " + drive + "/Users/x" + user + "\n",
            "number": "n = 12" + user + "34\n",
            "bare": "user " + user + " logged in\n",
            "file name": "out/" + user + ".txt\n",
        }
        for plants, expected in ((found, [("plant.txt", "a name of this machine")]), (clean, [])):
            for label, text in plants.items():
                with self.subTest(plant=label), tempfile.TemporaryDirectory() as scratch:
                    root = Path(scratch)
                    (root / "plant.txt").write_bytes(text.encode("utf-8"))
                    got = content_findings(root, rules.source_files(root), [], names)
                    self.assertEqual([f for f in got if f[1] == "a name of this machine"], expected)


class References(unittest.TestCase):
    """The reference rule from the text alone, on every system: each form a
    file of its own in a scratch tree, refused for a reference and for
    nothing else; each near miss a file of its own, clean."""

    # Each form holds a bar the case takes out, so that this file holds none.
    FORMS = ("by rul|ing 43.|3 item 3", "see sec|tion 44, item 7", "rul|ing #44", "the 44.3 rul|ing",
             "se|c. 44.3", "it|em 7 of 44.3", "rul|ing-44.3", "rul|ings_44", "&se|ct;44", "chall|enge 28's B1",
             "28|B1", "anom|aly 1242", "F|138", "per bri|ef 25", "S\xa7|44", "\xa7\xa7|44", "Rul|ing\xa044",
             "rul|ing\n44.3", "x\xa7|44", "as F|5's swap", "sec|tion 12", "44.|3 item 7")
    NEAR = ("# noqa: F821, given by a tool\n",
            "Python 3.15, CMake 4.4.3, version 1.2.3\n",
            "Sections 1 through 9 of the license, section 3\n",
            "steps 1 to 5, part 1, item 3 of 5, items 2 and 4\n",
            "a digest 28b1e0f5, 0x28B1, ff28B1, 0xF5, 1.5F, F1000, AF138, F5x, %F\n",
            "30 sec, 2 secs later, a brief note, in brief, 3 cases; a challenge, 2 challenges\n",
            "3 rulings, the ruling's word, a ruling: a type\n",
            "an anomaly, the anomalies seen\n",
            "{value:5F} and %-8F\n",
            "the sign \xa7 alone, \xa7 and a word\n")

    def test_forms(self):
        self.assertEqual((len(self.FORMS), len(self.NEAR)), (22, 10))
        forms = {f"form{i:02}.txt": form.replace("|", "") for i, form in enumerate(self.FORMS)}
        near = {f"near{i:02}.txt": text for i, text in enumerate(self.NEAR)}
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name, text in (forms | near).items():
                (root / name).write_bytes(text.encode("utf-8"))
            found = content_findings(root, rules.source_files(root), [], ())
        self.assertEqual(found, [(name, REFERENCE_FINDING) for name in sorted(forms)])
