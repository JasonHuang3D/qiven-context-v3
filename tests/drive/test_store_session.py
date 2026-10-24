"""The drive's store requests through the session, on each store: every
request once, each handle opened, read and closed, an id never given
twice, and one typed error of each kind the drive can reach on its own
(driver/store_ops.hpp). StoreBusy needs a second writer on the file, the
seams' child's (the machine cases): no test here names the engine's
module, which B1 keeps to the store. The store's own cases are
contract/'s S cases; these are the wire's.
"""

import os
import shutil
import stat
import tempfile
import unittest
from pathlib import Path

from tests.drive.session import FAKE, Drive, DriveError, StoreExport

SCOPE, LANE = "project:qiven", "implementer"
# The fixtures of one lane, in its order, and the forget at its end.
CHAIN = ("A", "B", "D", "ACC")
LIMITS = {"target_size": 1 << 20, "warning_size": 1 << 20, "max_size": 1 << 20,
          "target_count": 1, "warning_count": 1, "max_count": 1}


class StoreSession:
    STORE = None

    def setUp(self):
        super().setUp()
        self.scratch = Path(tempfile.mkdtemp(prefix="qiven3-ss-"))
        self.addCleanup(shutil.rmtree, self.scratch, True)

    def drive(self):
        return Drive("--root", str(self.scratch), "--store", self.STORE, drive=FAKE)

    def refused(self, call, *args, **kwargs):
        with self.assertRaises(DriveError) as caught:
            call(*args, **kwargs)
        return caught.exception

    def assert_store(self, error, cls, code=None):
        self.assertEqual((error.kind, error.cls), ("store", cls), error.error)
        for key in ("operation", "path", "detail"):
            self.assertIn(key, error.error)
        if code is not None:
            self.assertEqual(error.code, code, error.error)

    def test_requests(self):
        with self.drive() as d:
            entries = [d.fixture_entry(name) for name in CHAIN]
            forget = d.fixture_entry("E")
            self.assertIsNone(forget[1])
            store = d.store_make()
            ids = {store.id, store.state}
            self.assertIsNone(store.head(SCOPE, LANE))
            written = store.append(SCOPE, LANE, None, entries)
            head = written["head"]
            self.assertEqual(head["seq"], len(CHAIN))
            self.assertEqual(store.head(SCOPE, LANE), head)
            rows = store.heads()["rows"]
            self.assertEqual(rows, [{"scope": SCOPE, "lane": LANE, **head}])
            self.assertEqual(store.heads([SCOPE])["rows"], rows)
            written_id = None
            for line, body in entries:
                written_id = line.split(b'"id":"')[1].split(b'"')[0].decode()
                self.assertEqual(store.header(written_id), line)
                self.assertEqual(store.body(written_id), body)
                self.assertFalse(store.forgotten(written_id))
            self.assertEqual(written_id, head["id"])
            # A scan, read one header at a time, then from a seq.
            scan = store.scan()
            self.assertEqual(list(scan), [line for line, _ in entries])
            self.assertIsNone(scan.next())
            scan.close()
            later = store.scan(scope=SCOPE, lane=LANE, after=2)
            self.assertEqual(list(later), [line for line, _ in entries[2:]])
            later.close()
            # An export, read whole, then imported into a second state.
            export = store.export()
            snapshot = export.heads()
            self.assertEqual(snapshot["rows"], rows)
            self.assertEqual(snapshot["volume"]["count"], len(CHAIN))
            lines = list(export)
            self.assertEqual([(x["line"], x["body"]) for x in lines], entries)
            other = d.store_make()
            imported = other.import_((rows, lines))
            self.assertEqual(imported["counts"]["entries"], len(CHAIN))
            self.assertEqual(imported["rows"], rows)
            export.close()
            export.drop()
            again = store.export(scopes=[SCOPE], after=[(SCOPE, LANE, 3)])
            self.assertEqual(len(list(again)), 1)
            third = d.store_make()
            third_again = d.store_again(third.state)
            # An export of this drive as an import's source.
            self.assertEqual(third_again.import_(StoreExport(d, store.export().id))["counts"]["entries"],
                             len(CHAIN))
            again.drop()
            # The forget, its guard every head the store holds.
            forgot = store.forget(SCOPE, LANE, head["id"], [forget], [(SCOPE, LANE, head["id"])])
            self.assertEqual(forgot["head"]["seq"], len(CHAIN) + 1)
            self.assertTrue(forgot["erased"])
            erased = forgot["erased"][0]
            self.assertTrue(store.forgotten(erased))
            self.assertIsNone(store.body(erased))
            self.assertIn("done", store.purge())
            self.assertEqual(store.verify()["forgets"], 1)
            self.assertEqual(store.size()["volume"]["count"], len(CHAIN) + 1)
            self.assertEqual(store.limits()["max_count"], 550000)
            self.assertFalse(store.read_only())
            reader = d.store_read_only(store.state)
            self.assertTrue(reader.read_only())
            self.assertEqual(reader.head(SCOPE, LANE), forgot["head"])
            for made in (other, third, third_again, reader):
                self.assertNotIn(made.id, ids)
                ids |= {made.id, made.state}
            self.assertEqual(store.close(), {"done": True})
            reader.close()
            reader.drop()
            if d.store_capabilities()["durable"]:
                back = d.store_reopen(store.state)
                self.assertEqual(back.head(SCOPE, LANE), forgot["head"])
                back.close()
            else:
                error = self.refused(d.store_reopen, store.state)
                self.assertEqual(error.kind, "request")
                self.assertTrue(error.detail.startswith("not applicable: "), error.detail)

    def test_ids(self):
        """Each id is given once: a handle's id after its close is no
        handle's, and the next is new."""
        with self.drive() as d:
            store = d.store_make()
            store.append(SCOPE, LANE, None, [d.fixture_entry("A")])
            seen = {store.id, store.state}
            scan = store.scan()
            self.assertIsNotNone(scan.next())
            scan.close()
            self.assertEqual(self.refused(scan.next).kind, "request")
            self.assertEqual(self.refused(scan.close).kind, "request")
            for _ in range(3):
                for made in (store.scan(), store.export()):
                    self.assertNotIn(made.id, seen)
                    seen.add(made.id)
                    made.close()
                    if isinstance(made, StoreExport):
                        made.drop()
            again = d.store_again(store.state)
            self.assertNotIn(again.id, seen)
            # A state or an object with something held over it stays.
            held = store.scan()
            self.assertEqual(self.refused(store.drop).kind, "request")
            self.assertEqual(self.refused(d.state_drop, store.state).kind, "request")
            held.close()
            again.drop()
            store.drop()
            d.state_drop(store.state)
            self.assertEqual(self.refused(store.head, SCOPE, LANE).kind, "request")

    def test_errors(self):
        """One typed error of each kind the drive reaches alone, each with
        its fields."""
        with self.drive() as d:
            a, b = d.fixture_entry("A"), d.fixture_entry("B")
            store = d.store_make()
            store.append(SCOPE, LANE, None, [a])
            conflict = self.refused(store.append, SCOPE, LANE, None, [b])
            self.assert_store(conflict, "HeadConflict")
            self.assertEqual((conflict.error["scope"], conflict.error["lane"], conflict.error["expected"]),
                             (SCOPE, LANE, ""))
            invalid = self.refused(store.append, SCOPE, LANE, store.head(SCOPE, LANE)["id"], [(b"{}", None)])
            self.assert_store(invalid, "EntryInvalid")
            for key in ("code", "index", "id", "field", "pattern"):
                self.assertIn(key, invalid.error)
            missing = self.refused(store.header, "sha256:" + "0" * 64)
            self.assert_store(missing, "NotFound")
            self.assertEqual(missing.error["id"], "sha256:" + "0" * 64)
            full = d.store_make(limits=LIMITS)
            full.append(SCOPE, LANE, None, [a])
            over = self.refused(full.append, SCOPE, LANE, full.head(SCOPE, LANE)["id"], [b])
            self.assert_store(over, "StoreFull", "store_full")
            self.assertEqual((over.error["count"], over.error["max_count"]), (2, 1))
            for key in ("size", "max_size"):
                self.assertIn(key, over.error)
            not_empty = self.refused(store.import_, ([], []))
            self.assert_store(not_empty, "StoreNotEmpty")
            self.assertEqual(not_empty.error["count"], 1)
            budget = d.reply("store_header", store=store.id, id=store.head(SCOPE, LANE)["id"], budget=16)
            self.assertEqual((budget["error"]["kind"], budget["error"]["code"]), ("memory", "budget"), budget)
            d.call("refuse_pages", at=1)
            refused = self.refused(store.size)
            self.assertEqual(refused.kind, "platform", refused.error)
            store.close()
            closed = self.refused(store.size)
            self.assert_store(closed, "StoreError", "closed")
            self.assertEqual(closed.operation, "size")

    def needs_files(self, d):
        if not d.store_capabilities()["files"]:
            self.skipTest(f"not applicable: the {self.STORE} store lacks files")

    def test_files(self):
        """The SQLite store's errors of its files: StoreLocked and
        StoreCorrupt; not applicable on a store without files."""
        with self.drive() as d:
            self.needs_files(d)
            store = d.store_make()
            store.append(SCOPE, LANE, None, [d.fixture_entry("A")])
            store.close()
            # Locked: the file read-only, so SQLite opens it so in silence.
            os.chmod(store.path, stat.S_IREAD)
            try:
                locked = self.refused(d.store_again, store.state)
                self.assert_store(locked, "StoreLocked", "not_writable")
            finally:
                os.chmod(store.path, stat.S_IREAD | stat.S_IWRITE)
            # Corrupt: the file's bytes no database's.
            for suffix in ("-wal", "-shm"):
                Path(store.path + suffix).unlink(missing_ok=True)
            Path(store.path).write_bytes(b"not a database, " * 256)
            corrupt = self.refused(d.store_again, store.state)
            self.assert_store(corrupt, "StoreCorrupt", "not_a_store")


class Sqlite(StoreSession, unittest.TestCase):
    STORE = "sqlite"
    ctest_labels = ("fake",)


class Memory(StoreSession, unittest.TestCase):
    STORE = "memory"
    ctest_labels = ("fake",)
