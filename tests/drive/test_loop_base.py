"""The old places' measure, tools/loop_base.json, the same as its commit
gives it (tools/loop_base.py's rows_at, through tools/loop_git.py): a
clone that lacks the commit, a shallow one (CI's), skips the comparison,
naming the commit; the check still reads the file."""

import json
import unittest

from tools import loop_base


class Base(unittest.TestCase):
    ctest_git = True  # starts git: listed on the loop's build alone (foundation/devkit/test/runner/list_tests.py)

    def test_the_file_is_its_commit(self):
        """The file's text that of its commit's table files' rows' old paths."""
        kept = loop_base.DATA.read_text(encoding="utf-8")
        commit = json.loads(kept)["commit"]
        try:
            rows = loop_base.rows_at(loop_base.ROOT, commit)
        except LookupError as error:
            self.skipTest(f"{error}: fetch it to compare {loop_base.DATA.name} with it")
        self.assertEqual(loop_base.text(commit, rows), kept)

    def test_a_commit_missing(self):
        """A commit the repository lacks named, not read as no rows."""
        with self.assertRaisesRegex(LookupError, "the base 0{39}1 is not in the repository"):
            loop_base.rows_at(loop_base.ROOT, "0" * 39 + "1")


if __name__ == "__main__":
    unittest.main()
