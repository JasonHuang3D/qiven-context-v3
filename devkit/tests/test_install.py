"""The installer's removals, the one place it can lose data: it removes only
what the previous install's RECORD lists and the source no longer holds,
and never a path outside purelib."""

import tempfile
import unittest
from pathlib import Path

from qiven_devkit import install

PYPROJECT = '[project]\nname = "pkg"\nversion = "{}"\n'


def source(root, version, files):
    """A source tree under root: pyproject.toml of pkg at version, src/ holding files {path: text}."""
    root.mkdir(parents=True, exist_ok=True)
    (root / "pyproject.toml").write_text(PYPROJECT.format(version), encoding="utf-8")
    src = root / "src"
    for path, text in files.items():
        (src / path).parent.mkdir(parents=True, exist_ok=True)
        (src / path).write_text(text, encoding="utf-8")


class Removals(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.lib = self.root / "lib"
        self.lib.mkdir()

    def test_only_what_record_lists_and_the_source_dropped(self):
        """A dropped module goes with its cached bytecode and its emptied
        directory; a file RECORD does not list stays, in the package's
        directory or another's."""
        tree = self.root / "source"
        source(tree, "0.1.0", {"pkg/__init__.py": "", "pkg/a.py": "a = 1\n", "pkg/b.py": "b = 1\n",
                               "pkg/sub/c.py": "c = 1\n"})
        install.install(tree, self.lib)
        cache = self.lib / "pkg" / "__pycache__"
        cache.mkdir()
        (cache / "a.cpython-315.pyc").write_bytes(b"a")
        (cache / "b.cpython-315.pyc").write_bytes(b"b")
        (self.lib / "pkg" / "notes.txt").write_text("mine\n", encoding="utf-8")
        (self.lib / "other").mkdir()
        (self.lib / "other" / "b.py").write_text("other\n", encoding="utf-8")
        (tree / "src" / "pkg" / "b.py").unlink()
        (tree / "src" / "pkg" / "sub" / "c.py").unlink()
        (tree / "src" / "pkg" / "a.py").write_text("a = 2\n", encoding="utf-8")

        held, written, removed = install.install(tree, self.lib)

        self.assertEqual(removed, 3)
        self.assertFalse((self.lib / "pkg" / "b.py").exists())
        self.assertFalse((cache / "b.cpython-315.pyc").exists())
        self.assertFalse((self.lib / "pkg" / "sub").exists())
        self.assertEqual((self.lib / "pkg" / "a.py").read_text(encoding="utf-8"), "a = 2\n")
        self.assertTrue((cache / "a.cpython-315.pyc").is_file())
        self.assertEqual((self.lib / "pkg" / "notes.txt").read_text(encoding="utf-8"), "mine\n")
        self.assertEqual((self.lib / "other" / "b.py").read_text(encoding="utf-8"), "other\n")

    def test_never_outside_purelib(self):
        """An old install's RECORD naming a path outside purelib leaves it;
        what it names inside goes, its .dist-info with it."""
        outside = self.root / "outside.txt"
        outside.write_text("keep\n", encoding="utf-8")
        old = self.lib / "pkg-0.0.1.dist-info"
        old.mkdir()
        (self.lib / "pkg").mkdir()
        (self.lib / "pkg" / "old.py").write_text("old\n", encoding="utf-8")
        (old / "RECORD").write_text("../outside.txt,,\npkg/old.py,,\npkg-0.0.1.dist-info/RECORD,,\n",
                                    encoding="utf-8")
        tree = self.root / "source"
        source(tree, "0.1.0", {"pkg/__init__.py": ""})

        install.install(tree, self.lib)

        self.assertEqual(outside.read_text(encoding="utf-8"), "keep\n")
        self.assertFalse((self.lib / "pkg" / "old.py").exists())
        self.assertFalse(old.exists())
        self.assertTrue((self.lib / "pkg-0.1.0.dist-info" / "RECORD").is_file())


if __name__ == "__main__":
    unittest.main()
