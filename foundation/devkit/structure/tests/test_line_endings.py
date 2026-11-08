"""B32's other half: B32 itself, in every file git treats as text no byte
git cannot fix, is each module's check's (rules.<M>,
foundation/devkit/structure/checks.py, by rules.source_rules). Here, as git
reads the checkout's .gitattributes, the files git keeps byte for byte
KEPT's alone and the code beside them text with eol=lf, git under an
environment of the case's own:
foundation/devkit/python/git.py's reading_env, which reads no inherited GIT_
variable and no system or global config or attributes, so that its commands
read the same whatever the host's git config says (CI's Windows images set
core.autocrlf=true in the system config). tree.py reads the text attribute
under the same environment."""

import tempfile
import unittest

from foundation.devkit.python import git
from foundation.devkit.structure import rules

# The files kept byte for byte, .gitattributes' -text, each by what hashes
# or pins it: the fixtures and the vectors, compiled into the cases as their
# bytes; the fuzz corpora, each input named by its SHA-1 (a directory, by its
# trailing slash); the SQLite files, each pinned by its SHA-256 in
# foundation/system/sqlite/CMakeLists.txt. No other file is.
KEPT = ("tests/fixtures/entries.json", "tests/vectors/ids.json", "tests/fuzz/digest/regress/",
        "tests/fuzz/entries/regress/", "tests/fuzz/entry/regress/", "tests/fuzz/wire/regress/",
        "foundation/system/sqlite/src/sqlite3.c", "foundation/system/sqlite/include/qiven/sqlite/sqlite3.h")
# The directories that hold code beside kept data: their code stays text, LF.
BESIDE = ("tests/fixtures/", "tests/vectors/", "tests/support/fixtures/", "foundation/system/sqlite/")


def kept(rel):
    return rel in KEPT or rel.startswith(tuple(k for k in KEPT if k.endswith("/")))


class LineEndings(unittest.TestCase):
    def test_as_git_reads(self):
        """The files git keeps byte for byte, each tracked file's text
        attribute unset, are KEPT's, each of them tracked; and every other
        file beside them is text with eol=lf."""
        with tempfile.TemporaryDirectory() as scratch:
            env = git.reading_env(scratch)
            read = git.attributes(rules.ROOT, git.tracked(rules.ROOT, env=env), ("text", "eol"), env=env)
        git_reads = {rel: values["text"] != "unset" for rel, values in read.items()}
        self.assertEqual(sorted(rel for rel, text in git_reads.items() if not text),
                         sorted(rel for rel in git_reads if kept(rel)))
        self.assertEqual([k for k in KEPT if not any(rel == k or (k.endswith("/") and rel.startswith(k))
                                                     for rel in read)], [])
        beside = {rel: (values["text"], values["eol"]) for rel, values in read.items()
                  if rel.startswith(BESIDE) and not kept(rel)}
        self.assertTrue(beside)
        self.assertEqual({rel: v for rel, v in beside.items() if v != ("auto", "lf")}, {})
