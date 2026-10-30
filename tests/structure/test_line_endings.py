"""B32: in every file .gitattributes treats as text, every file outside its
-text paths, no byte git cannot fix: no lone CR (a CR before no LF) and no
control byte but tab and LF; a CR before an LF is git's to fix, and passes.
Each finding names the file and the first line holding such a byte. Over
every file of the checkout's working tree; over planted files, each byte
in a text file found with its line, a CRLF and a tab passing, a control
byte in each file kept byte for byte passing, and the same byte found in
the code and the unnamed data beside them; and the rule's reading of the
checkout's .gitattributes against git's own, with the files git keeps
byte for byte KEPT's alone and the code beside them text with eol=lf,
where git and the checkout's git directory are there, git under an
environment of the case's own (tests/drive/own_git.py)."""

import shutil
import tempfile
import unittest
from pathlib import Path

from tests.drive.own_git import git_env
from tests.structure import rules
from tools import loop_git

# The plants by their bytes: (name, bytes, the finding or None).
PLANTS = (
    ("src/x/lone.cpp", b"int a;\rint b;\n", "line 1: a lone CR"),
    ("src/x/late.cpp", b"\r\n\r\n\r\r\n", "line 3: a lone CR"),
    ("src/x/end.hpp", b"#pragma once\n\r", "line 2: a lone CR"),
    ("src/x/nul.hpp", b"#pragma once\n\x00\n", "line 2: the control byte 0x00"),
    ("tests/x/esc.py", b"#\n#\nx = '\x1b[0m'\n", "line 3: the control byte 0x1b"),
    ("tests/x/feed.cpp", b"int f;\x0c\n", "line 1: the control byte 0x0c"),
    ("tests/x/del.cpp", b"//\x7f\n", "line 1: the control byte 0x7f"),
    ("tests/x/CMakeLists.txt", b"#\n#\x0b\n", "line 2: the control byte 0x0b"),
    ("tests/fuzz/CMakeLists.txt", b"\x01", "line 1: the control byte 0x01"),
    ("README.md", b"a\n\x1f\n", "line 2: the control byte 0x1f"),
    ("src/x/crlf.hpp", b"#pragma once\r\nint x;\r\n", None),
    ("src/x/tab.cpp", b"\tint y;\n", None),
    ("tools/x.bat", b"@echo off\r\n", None),
    ("tests/fixtures/entries.json", b"\x00\r\x1b\x7f", None),
    ("tests/vectors/ids.json", b"\x00", None),
    ("tests/fuzz/digest/regress/0a", b"\x00\r", None),
    ("tests/fuzz/entry/regress/0b", b"\x1b\r", None),
    ("third_party/sqlite/sqlite3.c", b"\x0c\r\x00", None),
    ("third_party/sqlite/sqlite3.h", b"\r", None),
    ("tests/fixtures/other.json", b"{}\x00", "line 1: the control byte 0x00"),
    ("tests/vectors/more.json", b"{}\n\r", "line 2: a lone CR"),
    ("tests/support/fixtures/f.hpp", b"\r\x0c", "line 1: a lone CR"),
    ("tests/support/fixtures/CMakeLists.txt", b"#\n#\x0c\n", "line 2: the control byte 0x0c"),
    ("tests/drive/fixtures/n.cpp", b"\r", "line 1: a lone CR"),
    ("third_party/sqlite/CMakeLists.txt", b"#\r#\n", "line 1: a lone CR"),
    ("third_party/sqlite/README.md", b"a\n\x1b\n", "line 2: the control byte 0x1b"),
)
# The files kept byte for byte, .gitattributes' -text, each by what hashes
# or pins it: the fixtures and the vectors, compiled into the cases as their
# bytes; the fuzz corpora, each input named by its SHA-1 (a directory, by its
# trailing slash); the SQLite files, each pinned by its SHA-256 in
# CMakeLists.txt. No other file is.
KEPT = ("tests/fixtures/entries.json", "tests/vectors/ids.json", "tests/fuzz/digest/regress/",
        "tests/fuzz/entries/regress/", "tests/fuzz/entry/regress/", "tests/fuzz/wire/regress/",
        "third_party/sqlite/sqlite3.c", "third_party/sqlite/sqlite3.h")
# The directories that hold code beside kept data: their code stays text, LF.
BESIDE = ("tests/fixtures/", "tests/vectors/", "tests/support/fixtures/", "tests/drive/fixtures/", "third_party/")


def kept(rel):
    return rel in KEPT or rel.startswith(tuple(k for k in KEPT if k.endswith("/")))


def line_ending_findings(root):
    out = rules.Findings()
    rules.source_rules(root, rules.source_files(root), out)
    return sorted((f.where, f.detail) for f in out.found if f.rule == "B32")


class LineEndings(unittest.TestCase):
    ctest_reads = (".",)
    ctest_git = True  # starts git: listed on the loop's build alone (tests/runner/list_tests.py)

    def test_tree(self):
        self.assertEqual(line_ending_findings(rules.ROOT), [])

    def test_planted(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            shutil.copyfile(rules.ROOT / ".gitattributes", root / ".gitattributes")
            for name, data, _ in PLANTS:
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_bytes(data)
            found = line_ending_findings(root)
        self.assertEqual(found, sorted((name, finding) for name, _, finding in PLANTS if finding))

    def test_as_git_reads(self):
        """Each tracked file's text attribute, as the rule reads the
        checkout's .gitattributes, is git's own: unset under -text alone;
        the files git keeps byte for byte are KEPT's, each of them tracked;
        and every other file beside them is text with eol=lf."""
        git = loop_git.which()  # git's own reading: the real git, 2 processes
        if not git or not (rules.ROOT / ".git").exists():
            self.skipTest("git or the checkout's git directory is missing")
        with tempfile.TemporaryDirectory() as scratch:
            env = git_env(scratch)
            listed = loop_git.run(git, ["ls-files", "-z"], cwd=rules.ROOT, env=env, timeout=120, check=True).stdout
            done = loop_git.run(git, ["check-attr", "-z", "--stdin", "text", "eol"], cwd=rules.ROOT, env=env,
                                data=listed, timeout=120, check=True)
        fields = done.stdout.decode("utf-8").split("\0")
        read = {}
        for i in range(0, len(fields) - 2, 3):
            read.setdefault(fields[i], {})[fields[i + 1]] = fields[i + 2]
        git_reads = {rel: values["text"] != "unset" for rel, values in read.items()}
        attributes = rules.text_attributes(rules.ROOT)
        self.assertEqual({rel: rules.is_text(rel, attributes) for rel in git_reads}, git_reads)
        self.assertEqual(sorted(rel for rel, text in git_reads.items() if not text),
                         sorted(rel for rel in git_reads if kept(rel)))
        self.assertEqual([k for k in KEPT if not any(rel == k or (k.endswith("/") and rel.startswith(k))
                                                     for rel in read)], [])
        beside = {rel: (values["text"], values["eol"]) for rel, values in read.items()
                  if rel.startswith(BESIDE) and not kept(rel)}
        self.assertTrue(beside)
        self.assertEqual({rel: v for rel, v in beside.items() if v != ("auto", "lf")}, {})
