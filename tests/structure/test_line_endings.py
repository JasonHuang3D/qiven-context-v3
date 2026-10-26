"""B32: in every file .gitattributes treats as text, every file outside its
-text paths, no byte git cannot fix: no lone CR (a CR before no LF) and no
control byte but tab and LF; a CR before an LF is git's to fix, and passes.
Each finding names the file and the first line holding such a byte. Over
every file of the checkout's working tree; over planted files, each byte
in a text file found with its line, a CRLF and a tab passing, a control
byte under each -text path passing; and the rule's reading of the
checkout's .gitattributes against git's own, where git and the checkout's
git directory are there."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.structure import rules

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
    ("tests/fixtures/data.json", b"\x00\r\x1b\x7f", None),
    ("tests/support/fixtures/f.hpp", b"\r\x0c", None),
    ("tests/drive/fixtures/n.cpp", b"\r", None),
    ("tests/vectors/v.json", b"\x00", None),
    ("tests/fuzz/digest/regress/0a", b"\x00\r", None),
    ("tests/fuzz/entry/regress/0b", b"\x1b\r", None),
    ("third_party/sqlite/sqlite3.c", b"\x0c\r\x00", None),
)


def line_ending_findings(root):
    out = rules.Findings()
    rules.source_rules(root, rules.source_files(root), out)
    return sorted((f.where, f.detail) for f in out.found if f.rule == "B32")


def own_git(scratch):
    """git's environment for the comparison: no inherited GIT_ variable, no
    system or global config or attributes file, so that git reads the
    checkout's own attributes alone."""
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    return dict(env, GIT_CONFIG_NOSYSTEM="1", GIT_ATTR_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                XDG_CONFIG_HOME=scratch, HOME=scratch)


class LineEndings(unittest.TestCase):
    ctest_reads = (".",)

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
        checkout's .gitattributes, is git's own: unset under -text alone."""
        git = shutil.which("git")
        if not git or not (rules.ROOT / ".git").exists():
            self.skipTest("git or the checkout's git directory is missing")
        with tempfile.TemporaryDirectory() as scratch:
            env = own_git(scratch)
            listed = subprocess.run([git, "ls-files", "-z"], cwd=rules.ROOT, env=env, capture_output=True,
                                    timeout=120, check=True).stdout
            done = subprocess.run([git, "check-attr", "-z", "--stdin", "text"], cwd=rules.ROOT, env=env,
                                  input=listed, capture_output=True, timeout=120, check=True)
        fields = done.stdout.decode("utf-8").split("\0")
        git_reads = {fields[i]: fields[i + 2] != "unset" for i in range(0, len(fields) - 2, 3)}
        attributes = rules.text_attributes(rules.ROOT)
        self.assertEqual({rel: rules.is_text(rel, attributes) for rel in git_reads}, git_reads)
        self.assertIn(False, git_reads.values())
