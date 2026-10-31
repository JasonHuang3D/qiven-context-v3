"""The platform's cases P27 to P30, each defined once over an adapter
(contract/platform_cases.py names the binding's part).

Here: P27 (create and read, and two drives' creates racing, and
remove_directory, create's counterpart for a directory, and list, remove
and rename, the kept files' members), P28 (pages),
P29 (normcase) and P30 (wipe, which digest.Key's ends and moves call;
its unit cases count those calls on the fake). The Windows adapter's
create and read are its module win/files', in package 5; until then a
binding with CREATE_READ false checks that each answers unsupported, and
the case is skipped.
"""

import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from tests.drive.session import has_faults

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

# P27's race: the paths two drives create at once, and how long a drive
# waits for the other at each.
RACES = 64
GATE_S = 30

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

    def test_P27_remove_directory(self):
        """remove_directory of an empty directory, of none, of one holding a
        file and of one holding a directory, of a file and of a relative
        path; on the fake, refused by its fault point."""
        self.needs("files")
        root = Path(self.files_root())
        empty, full, nested, plain = root / "empty", root / "full", root / "nested", root / "plain"
        with self.drive() as d:
            if not self.CREATE_READ:
                empty.mkdir()
                error = self.failure(d.call, "remove_directory", path=str(empty))
                self.assert_platform_error(error, "unsupported")
                self.assertTrue(empty.is_dir())
                self.skipTest("until package 5: the Windows adapter's remove_directory is win/files'")
            empty.mkdir()
            self.assertIs(d.call("remove_directory", path=str(empty)), True)
            self.assertFalse(empty.exists())
            self.assertIs(d.call("remove_directory", path=str(empty)), True)
            full.mkdir()
            (full / "inside").write_bytes(b"inside")
            (nested / "inner").mkdir(parents=True)
            for kept in (full, nested):
                self.assertIs(d.call("remove_directory", path=str(kept)), False)
            self.assertEqual((full / "inside").read_bytes(), b"inside")
            self.assertTrue((nested / "inner").is_dir())
            plain.write_bytes(b"plain")
            self.assert_platform_error(self.failure(d.call, "remove_directory", path=str(plain)), "io")
            self.assertEqual(plain.read_bytes(), b"plain")
            self.assert_platform_error(self.failure(d.call, "remove_directory", path="relative"), "path")
            if self.ADAPTER == "fake" and has_faults():
                empty.mkdir()
                # The drive's pages first, then the member.
                reply = d.reply("remove_directory", path=str(empty), fault={"k": 2, "of": "platform"})
                error = reply["error"]
                self.assertEqual((error["kind"], error["code"], error["operation"]),
                                 ("platform", "io", "remove_directory"), reply)
                self.assertTrue(empty.is_dir())

    def test_P27_names(self):
        """list of a directory's names sorted by their UTF-8 bytes, never
        . or ..; remove with none there no fault; rename never replacing,
        exists with both kept; each one's path refused; on the fake,
        refused by its fault point."""
        self.needs("files")
        root = Path(self.files_root())
        kept = root / "kept"
        kept.mkdir()
        for name in ("b.tmp", "Z.pending", "a", "\xe4"):
            (kept / name).write_bytes(name.encode())
        (kept / "dir").mkdir()
        with self.drive() as d:
            self.assertEqual(d.call("list", path=str(kept)), ["Z.pending", "a", "b.tmp", "dir", "\xe4"])
            self.assertIsNone(d.call("remove", path=str(kept / "a")))
            self.assertIsNone(d.call("remove", path=str(kept / "a")))
            self.assertIsNone(d.call("rename", path=str(kept / "b.tmp"), name="b.pending"))
            self.assertEqual(d.call("list", path=str(kept)), ["Z.pending", "b.pending", "dir", "\xe4"])
            (kept / "c.tmp").write_bytes(b"c")
            for there in ("b.pending", "dir"):
                error = self.failure(d.call, "rename", path=str(kept / "c.tmp"), name=there)
                self.assert_platform_error(error, "exists")
            self.assertEqual((kept / "c.tmp").read_bytes(), b"c")
            self.assertEqual((kept / "b.pending").read_bytes(), b"b.tmp")
            # Its own name: renamed to itself, kept.
            self.assertIsNone(d.call("rename", path=str(kept / "c.tmp"), name="c.tmp"))
            self.assertEqual((kept / "c.tmp").read_bytes(), b"c")
            self.assert_platform_error(self.failure(d.call, "list", path=str(kept / "missing")), "io")
            self.assert_platform_error(self.failure(d.call, "remove", path=str(kept / "dir")), "io")
            self.assert_platform_error(self.failure(d.call, "rename", path=str(kept / "c.tmp"), name="x/y"), "path")
            for op in ("list", "remove"):
                self.assert_platform_error(self.failure(d.call, op, path="relative"), "path")
            if self.ADAPTER == "fake" and has_faults():
                for op, args in (("list", {"path": str(kept)}), ("remove", {"path": str(kept / "c.tmp")}),
                                 ("rename", {"path": str(kept / "c.tmp"), "name": "c.pending"})):
                    # The drive's pages first, then the member.
                    reply = d.reply(op, fault={"k": 2, "of": "platform"}, **args)
                    error = reply["error"]
                    self.assertEqual((error["kind"], error["code"], error["operation"]),
                                     ("platform", "io", op), reply)
                self.assertEqual((kept / "c.tmp").read_bytes(), b"c")

    def test_P27_create_race(self):
        """Two drives on one root create the same absent files, each pair of
        creates released together: of each pair one makes the empty file and
        the other finds it present, never both."""
        self.needs("files")
        if not self.CREATE_READ:
            self.skipTest("until package 5: the Windows adapter's create is win/files'")
        paths = [Path(self.files_root()) / f"raced-{k}" for k in range(RACES)]
        gate = threading.Barrier(2, timeout=GATE_S)

        def creates(d):
            outcomes = []
            for path in paths:
                gate.wait()
                reply = d.reply("create", path=str(path))
                outcomes.append("made" if reply.get("ok") else (reply["error"]["kind"], reply["error"]["code"]))
            return outcomes

        with self.drive() as first, self.drive() as second, ThreadPoolExecutor(2) as pool:
            both = list(pool.map(creates, (first, second)))
        for path, pair in zip(paths, zip(*both)):
            with self.subTest(path=path.name):
                self.assertEqual(sorted(pair, key=str), [("platform", "exists"), "made"])
                self.assertEqual(path.read_bytes(), b"")

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
                error = self.failure(d.call, "pages", n=(1 << 53) - 1)
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

    def test_P30_wipe(self):
        """wipe of 32 bytes the drive filled with 0xa5, then of an empty
        span: the 32 bytes read back zero; nothing for the empty span, no
        error."""
        with self.drive() as d:
            self.assertEqual(d.call("wipe", n=32), "00" * 32)
            self.assertEqual(d.call("wipe", n=0), "")
            if self.ADAPTER == "fake":
                self.assertEqual(d.call("calls").count("wipe"), 2)
