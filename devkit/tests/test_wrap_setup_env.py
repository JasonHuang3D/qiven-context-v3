"""A setup script's environment kept between runs: the same key reads the kept
one and starts no setup; a changed key captures afresh. A wrong reuse would
hand a later build another setup's environment; a lost one starts a slow
setup on every run."""

import os
import sys
import tempfile
import unittest
from pathlib import Path

from qiven_devkit.wrap import setup_env

# Counts its starts in starts.txt beside it, and leaves two variables: one
# from the base, one from its first argument.
SETUP = ('@echo started>>"%~dp0starts.txt"\r\n'
         '@set "QIVEN_TEST_ADDED=%QIVEN_TEST_BASE%-added"\r\n'
         '@set "QIVEN_TEST_ARG=%~1"\r\n'
         "@exit /b 0\r\n")
BASE_VALUE = "caf\N{LATIN SMALL LETTER E WITH ACUTE}-\N{CJK UNIFIED IDEOGRAPH-4E2D}"
LIMIT = 60


@unittest.skipUnless(sys.platform == "win32", "a batch file run by cmd")
class Kept(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        root = Path(scratch.name)
        self.script, self.input, self.store = root / "setup.cmd", root / "input.txt", root / "store"
        self.script.write_bytes(SETUP.encode("ascii"))
        self.input.write_text("one\n", encoding="utf-8")
        self.starts = root / "starts.txt"
        self.base = {**os.environ, "QIVEN_TEST_BASE": BASE_VALUE}

    def environment(self, word):
        """What the setup leaves with `word`, by a capturer of its own, as a
        later run would have."""
        capturer = setup_env.SetupEnvironment(os.environ["ComSpec"], self.store, LIMIT)
        return capturer.environment(self.script, [word], self.base, {"QIVEN_TEST_ARG": word}, inputs=[self.input])

    def started(self):
        return len(self.starts.read_text(encoding="ascii").splitlines()) if self.starts.is_file() else 0

    def test_same_key_reads_kept_changed_key_captures(self):
        first = self.environment("one")
        self.assertEqual(first["QIVEN_TEST_ADDED"], f"{BASE_VALUE}-added")
        self.assertEqual(self.started(), 1)

        self.assertEqual(self.environment("one"), first)
        self.assertEqual(self.started(), 1, "the same key started the setup")

        self.assertEqual(self.environment("two")["QIVEN_TEST_ARG"], "two")
        self.assertEqual(self.started(), 2, "changed arguments read a kept environment")
        self.assertEqual(self.environment("one"), first)
        self.assertEqual(self.started(), 2, "the first key's kept environment was lost")

        self.script.write_bytes((SETUP + "@rem changed\r\n").encode("ascii"))
        self.assertEqual(self.environment("one")["QIVEN_TEST_ADDED"], f"{BASE_VALUE}-added")
        self.assertEqual(self.started(), 3, "a changed script read a kept environment")

        self.input.write_text("two\n", encoding="utf-8")
        self.environment("one")
        self.assertEqual(self.started(), 4, "a changed input read a kept environment")

        self.assertEqual(sorted(path.suffix for path in self.store.iterdir()), [".json"] * 4,
                         "the store holds other than one kept file per key")


if __name__ == "__main__":
    unittest.main()
