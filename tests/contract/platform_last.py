"""The platform's cases P27 to P30, each defined once over an adapter
(contract/platform_cases.py names the binding's part).

Here: P27 (create and read), P28 (pages) and P29 (normcase). P30 (wipe)
comes with package 2's digest.Key, its first user. The Windows adapter's
create and read are its module win/files', in package 5; until then a
binding with CREATE_READ false checks that each answers unsupported, and
the case is skipped.
"""

from pathlib import Path

# normcase's vectors: each path, and its form on the Windows adapter, by
# LCMapStringEx's invariant lowercase with '/' as '\'.
NORMCASE = (
    ("C:/Users/ABC", "c:\\users\\abc"),
    ("D:\\\u00c4\\\u00d6x", "d:\\\u00e4\\\u00f6x"),
    ("C:\\\u0130", "c:\\\u0130"),
    ("C:\\\u01c5", "c:\\\u01c5"),
    ("C:\\\u00df", "c:\\\u00df"),
    ("C:\\\u03a3\u03a3", "c:\\\u03c3\u03c3"),
    ("C:\\\uff21", "c:\\\uff41"),
    ("C:\\\U00010400", "c:\\\U00010428"),
)

# Every byte value, a CRLF and the C runtime's end of text among them.
WHOLE = bytes(range(256)) + b"\r\n\x1a\x00tail"


def ascii_folded(path):
    """The fake's form: ASCII letters alone folded."""
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in path)


class PlatformLast:
    CREATE_READ = True

    def test_P27_create_and_read(self):
        """create of an absent file, then of an absent directory; each again;
        one whose parent is missing; read of a file the test wrote, of a
        missing one, and of one whose error the fake is given."""
        self.needs("files")
        root = Path(self.files_root())
        made, folder = root / "made", root / "folder"
        with self.drive() as d:
            if not self.CREATE_READ:
                for op in ("create", "read"):
                    error = self.failure(d.call, op, path=str(made))
                    self.assert_platform_error(error, "unsupported")
                self.assertFalse(made.exists())
                self.skipTest("until package 5: the Windows adapter's create and read are win/files'")
            self.assertIsNone(d.call("create", path=str(made)))
            self.assertTrue(made.is_file())
            self.assertEqual(made.read_bytes(), b"")
            self.assertIsNone(d.call("create", path=str(folder), directory=True))
            self.assertTrue(folder.is_dir())

            made.write_bytes(b"kept")
            (folder / "inside").write_bytes(b"inside")
            self.assert_platform_error(self.failure(d.call, "create", path=str(made)), "exists")
            self.assert_platform_error(
                self.failure(d.call, "create", path=str(folder), directory=True), "exists")
            self.assertEqual(made.read_bytes(), b"kept")
            self.assertEqual((folder / "inside").read_bytes(), b"inside")

            orphan = root / "missing" / "child"
            for directory in (False, True):
                error = self.failure(d.call, "create", path=str(orphan), directory=directory)
                self.assert_platform_error(error, "io")
            self.assertFalse(orphan.parent.exists())
            self.assert_platform_error(self.failure(d.call, "create", path="relative"), "path")

            written = root / "written.bin"
            written.write_bytes(WHOLE)
            self.assertEqual(bytes.fromhex(d.call("read", path=str(written))), WHOLE)
            self.assertEqual(bytes.fromhex(d.call("read", path=str(made))), b"kept")

            error = self.failure(d.call, "read", path=str(root / "absent"))
            self.assert_platform_error(error, "io")
            self.assertTrue(error.detail, "read's io names the system's error")
            if self.ADAPTER == "fake":
                planted = "ERROR_LOCK_VIOLATION (33)"
                d.call("set_read_error", path=str(written), detail=planted)
                error = self.failure(d.call, "read", path=str(written))
                self.assert_platform_error(error, "io")
                self.assertEqual(error.detail, planted)

    def test_P28_pages(self):
        """pages(1), then pages(5000); the fake set to refuse the second; on
        the adapter, a count the system refuses."""
        with self.drive() as d:
            first = d.call("pages", n=1)
            self.assertEqual((first["size"], first["zero"]), (4096, True))
            second = d.call("pages", n=5000)
            self.assertEqual((second["size"], second["zero"]), (8192, True))
            third = d.call("pages", n=4096)
            self.assertEqual(third["size"], 4096)
            for held in (first, second, third):
                self.assertIsNone(d.call("pages_free", id=held["id"]))
            if self.ADAPTER != "fake":
                # Pages the system refuses. Not on the fake: under the asan
                # build's allocator such a request ends the process.
                error = self.failure(d.call, "pages", n=1 << 62)
                self.assert_platform_error(error, "memory")
                return
            before = d.call("lent")
            d.call("refuse_pages", at=2)
            first = d.call("pages", n=1)
            self.assert_platform_error(self.failure(d.call, "pages", n=5000), "memory")
            self.assertEqual(d.call("pages_check", id=first["id"]), {"size": 4096, "zero": True})
            self.assertEqual(d.call("lent"), before + 4096)
            d.call("pages_free", id=first["id"])
            self.assertEqual(d.call("lent"), before)
            last = d.call("pages", n=1)
            self.assertEqual(last["size"], 4096)
            d.call("pages_free", id=last["id"])

    def test_P29_normcase(self):
        """normcase of the text's paths: on the adapter, the table's forms;
        on the fake, ASCII letters alone folded."""
        with self.drive() as d:
            for path, folded in NORMCASE:
                with self.subTest(path=path):
                    expected = ascii_folded(path) if self.ADAPTER == "fake" else folded
                    self.assertEqual(d.call("normcase", path=path), expected)
            self.assertEqual(d.call("normcase", path=""), "")
