"""B32: in every file .gitattributes treats as text, every file outside its
-text paths, no byte git cannot fix: no lone CR (a CR before no LF) and no
control byte but tab and LF; a CR before an LF is git's to fix, and passes.
Each finding names the file and the first line holding such a byte. Over
every file of the checkout's working tree; and the rule's reading of the
checkout's .gitattributes against git's own, with the files git keeps
byte for byte KEPT's alone and the code beside them text with eol=lf,
where git and the checkout's git directory are there, git under an
environment of the case's own (tests/drive/own_git.py)."""

import tempfile
import unittest

from tests.drive.own_git import git_env
from foundation.devkit.structure import rules
from tools import loop_git

# The files kept byte for byte, .gitattributes' -text, each by what hashes
# or pins it: the fixtures and the vectors, compiled into the cases as their
# bytes; the fuzz corpora, each input named by its SHA-1 (a directory, by its
# trailing slash); the SQLite files, each pinned by its SHA-256 in
# CMakeLists.txt. No other file is.
KEPT = ("tests/fixtures/entries.json", "tests/vectors/ids.json", "tests/fuzz/digest/regress/",
        "tests/fuzz/entries/regress/", "tests/fuzz/entry/regress/", "tests/fuzz/wire/regress/",
        "third_party/sqlite/sqlite3.c", "third_party/sqlite/sqlite3.h")
# The directories that hold code beside kept data: their code stays text, LF.
BESIDE = ("tests/fixtures/", "tests/vectors/", "tests/support/fixtures/", "third_party/")


def kept(rel):
    return rel in KEPT or rel.startswith(tuple(k for k in KEPT if k.endswith("/")))


def line_ending_findings(root):
    out = rules.Findings()
    rules.source_rules(root, rules.source_files(root), out)
    return sorted((f.where, f.detail) for f in out.found if f.rule == "B32")


class LineEndings(unittest.TestCase):
    ctest_git = True  # starts git: listed on the git tests' build alone (foundation/devkit/test/runner/list_tests.py)

    def test_tree(self):
        self.assertEqual(line_ending_findings(rules.ROOT), [])

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
