"""git's listing decoded whole, against a real git in a repository of the
test's own: a name lost, cut or decoded wrong would leave a file out of the
content id the store keeps."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

from qiven_devkit.wrap import call, git

LIMIT = 60
ODD = "caf\N{LATIN SMALL LETTER E WITH ACUTE} \N{CJK UNIFIED IDEOGRAPH-4E2D}.txt"


@unittest.skipUnless(sys.platform == "win32", "wrap runs on Windows only")
class Listing(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        root = Path(scratch.name)
        (root / "config").mkdir()
        self.env = git.steady_env(dict(os.environ), root / "config")
        self.repo = root / "repo"
        self.repo.mkdir()
        self.run_git("git init", ["init", "-q"])
        files = {"a b.txt": "a\n", "gone.txt": "gone\n", ".gitignore": "ignored.txt\n", "ignored.txt": "no\n",
                 f"sub/{ODD}": "odd\n"}
        for name, text in files.items():
            (self.repo / name).parent.mkdir(parents=True, exist_ok=True)
            (self.repo / name).write_text(text, encoding="utf-8")
        self.run_git("git add", ["add", "--", "a b.txt", "gone.txt"])
        (self.repo / "gone.txt").unlink()

    def run_git(self, name, args):
        call.call(name, "git", args, env=self.env, cwd=self.repo, limit=LIMIT)

    def test_tracked_and_untracked_not_ignored(self):
        """Tracked (one gone from the work tree), untracked, a name with a
        space and one with non-ASCII; the ignored one left out."""
        listed = git.Git("git", self.env, LIMIT).listing(self.repo)
        self.assertEqual(sorted(listed), sorted(["a b.txt", "gone.txt", ".gitignore", f"sub/{ODD}"]))
        self.assertEqual(len(listed), len(set(listed)))

    def test_from_a_folder(self):
        """A folder's listing names its files from the folder."""
        self.assertEqual(git.Git("git", self.env, LIMIT).listing(self.repo / "sub"), [ODD])


if __name__ == "__main__":
    unittest.main()
