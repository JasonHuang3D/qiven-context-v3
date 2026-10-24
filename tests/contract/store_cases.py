"""The store's cases, each defined once over a store, through the fake's
drive started with --store.

A binding (contract/test_store_sqlite.py, contract/test_store_memory.py)
mixes these into a TestCase with its store: STORE, the name --store takes
and the drive gives; CAPABILITIES, what the format says it declares of
durable, files and processes. Each case starts its own drive over a
scratch root of its own and makes its states through the drive's factory
(drive.store_make(), then store_again, store_read_only and store_reopen
on the state).

A case names its tags, durable, files, processes or "own: sqlite", by
needs(drive, ...), never an implementation: a tag the store lacks, as the
drive reports its capabilities, is not applicable, the skip with its
reason, "not applicable: the memory store lacks durable", which the suite
gives CTest as its skip code. test_tags checks that frame on the binding's
store.

Here: S1. S2 onward come with the store's operations.
"""

import shutil
import tempfile
import unittest
from pathlib import Path

from tests.drive.session import FAKE, Drive, DriveError

# The tags a case may name, in the order a reason lists them.
TAGS = ("durable", "files", "processes", "own: sqlite")


class StoreBinding:
    STORE = None
    CAPABILITIES = {}

    def setUp(self):
        super().setUp()
        self.scratch = Path(tempfile.mkdtemp(prefix="qiven3-s-"))
        self.addCleanup(shutil.rmtree, self.scratch, True)

    def drive(self, seed=None):
        """The fake's drive over this case's root, the binding's store
        selected; a context manager."""
        root = self.scratch / "root"
        root.mkdir(exist_ok=True)
        args = ["--root", str(root), "--store", self.STORE]
        if seed is not None:
            args += ["--seed", seed]
        return Drive(*args, drive=FAKE)

    @staticmethod
    def lacking(capabilities, tags):
        """The tags of `tags` a store of these capabilities lacks, in TAGS'
        order."""
        held = {tag for tag in ("durable", "files", "processes") if capabilities[tag]}
        if capabilities["own"] == "sqlite":
            held.add("own: sqlite")
        unknown = set(tags) - set(TAGS)
        if unknown:
            raise ValueError(f"no tag {sorted(unknown)}: a case names {', '.join(TAGS)}")
        return [tag for tag in TAGS if tag in tags and tag not in held]

    def needs(self, drive, *tags):
        """A skip, "not applicable: ...", where the drive's store lacks a tag."""
        capabilities = drive.store_capabilities()
        lacking = self.lacking(capabilities, tags)
        if lacking:
            self.skipTest(f"not applicable: the {capabilities['name']} store lacks {', '.join(lacking)}")

    def failure(self, call, *args, **kwargs):
        """The DriveError the call raises; a failure if it raises none."""
        with self.assertRaises(DriveError) as caught:
            call(*args, **kwargs)
        return caught.exception

    def assert_store_error(self, error, cls, code=None, **fields):
        """error is the store's typed error of class cls, its code and each
        field given as stated."""
        self.assertEqual((error.kind, error.cls), ("store", cls), error.error)
        if code is not None:
            self.assertEqual(error.code, code, error.error)
        for key, value in fields.items():
            self.assertEqual(error.error.get(key), value, f"{key}: {error.error}")


class StoreCases:
    def test_tags(self):
        """The frame: each tag the binding's store lacks is not applicable,
        with its reason; each it holds runs."""
        with self.drive() as drive:
            capabilities = drive.store_capabilities()
            for tag in TAGS:
                has = self.STORE == "sqlite" if tag == "own: sqlite" else self.CAPABILITIES[tag]
                with self.subTest(tag=tag):
                    if has:
                        self.needs(drive, tag)
                        continue
                    with self.assertRaises(unittest.SkipTest) as skipped:
                        self.needs(drive, tag)
                    self.assertEqual(str(skipped.exception), f"not applicable: the {self.STORE} store lacks {tag}")
            if not all(self.CAPABILITIES.values()):
                with self.assertRaises(unittest.SkipTest) as skipped:
                    self.needs(drive, *TAGS)
                lacking = ", ".join(self.lacking(capabilities, TAGS))
                self.assertEqual(str(skipped.exception), f"not applicable: the {self.STORE} store lacks {lacking}")
            self.needs(drive)

    def test_S1_capabilities(self):
        """S1: the store declares durable, files and processes: SQLite all
        true, the in-memory store all false. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            capabilities = drive.store_capabilities()
            self.assertEqual(capabilities["name"], self.STORE)
            got = {tag: capabilities[tag] for tag in ("durable", "files", "processes")}
            self.assertEqual(got, self.CAPABILITIES)
            for value in got.values():
                self.assertIs(type(value), bool)
