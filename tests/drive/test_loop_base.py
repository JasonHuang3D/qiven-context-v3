"""The old places' measure, tools/loop_base.json: its commit the one this
test pins, named here, not read from the file, so that the base moves only
with this line; the file the same as its commit gives it
(tools/loop_base.py's rows_at, through tools/loop_git.py). The commit
missing fails the comparison, but in a shallow clone (CI's), which skips
it by name; the pin and the check's reading of the file still run."""

import json
import unittest

from tools import loop_base

PINNED = "2971434f092f0ae1dd74fa8ae81f9c319d1165ed"  # the base commit, its full hash


class Base(unittest.TestCase):
    ctest_git = True  # starts git: listed on the loop's build alone (foundation/devkit/test/runner/list_tests.py)

    def kept(self):
        """The file's text and its commit."""
        kept = loop_base.DATA.read_text(encoding="utf-8")
        return kept, json.loads(kept)["commit"]

    def seen(self, read, commit):
        """read(ROOT, commit); in a shallow clone that lacks it, the test skipped, naming both."""
        try:
            return read(loop_base.ROOT, commit)
        except LookupError as error:
            if not loop_base.shallow(loop_base.ROOT):
                raise
            self.skipTest(f"{error}: a shallow clone: fetch it to compare {loop_base.DATA.name} with it")

    def test_the_base_pinned(self):
        """The file names the pinned commit, by its full hash, with no git."""
        self.assertEqual(self.kept()[1], PINNED)

    def test_the_file_is_its_commit(self):
        """The file's text that of its commit's table files' rows' old paths."""
        kept, commit = self.kept()
        self.assertEqual(loop_base.text(commit, self.seen(loop_base.rows_at, commit)), kept)

    def test_a_short_hash_named_whole(self):
        """The script names the base by its full hash, whatever form it is given."""
        self.assertEqual(self.seen(loop_base.full, PINNED[:7]), PINNED)

    def test_a_commit_missing(self):
        """A commit the repository lacks named, not read as no rows."""
        with self.assertRaisesRegex(LookupError, "the base 0{39}1 is not in the repository"):
            loop_base.rows_at(loop_base.ROOT, "0" * 39 + "1")


if __name__ == "__main__":
    unittest.main()
