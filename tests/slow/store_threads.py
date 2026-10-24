"""S21, defined once over a store, through the fake's drive started with
--store: M5 and M6 with threads, each thread with a store object of its
own, and a call on a store object, a scan or an export from another thread
than its maker's. Mixed into the slow group's bindings
(slow/test_store_sqlite.py, slow/test_store_memory.py) with the contract
cases' frame (contract/store_cases.py's StoreBinding): the full suite only.

The threads are the drive's (store_race): each makes its own fake, its own
stage book and its own store object by the factory's again, waits for the
rest, then appends; the drive replies once all have ended.
"""

import json
import re

from tests.contract.store_entries import (LANES, SCOPE, Maker, canonical, content_text, header, id_of, same_snapshot,
                                          scanned, snapshot, writer_of)
from tests.drive.session import DriveError, entry_wire

# M5's and M6's floors: the changes of writer along M5's chain [proposal].
M5_CHANGES = 34
CLAIM = re.compile(r"Thread (\d+) number (\d+)\.")


class StoreThreads:
    def race(self, drive, store, m, lanes, count):
        """The threads' appends, one thread a lane given, count each; each
        thread's result, none ended by an error other than its retries'."""
        content = m.content("A")
        content["claim"] = "Thread {thread} number {number}."
        like = header(m.fixture("A"))["writer"]
        threads = [(lane, writer_of(lane, like)) for lane in lanes]
        got = drive.store_race(store.state, SCOPE, threads, count, canonical(content))
        for i, t in enumerate(got):
            self.assertNotIn("error", t, f"thread {i}: every failure HeadConflict or StoreBusy")
            self.assertEqual(len(t["ids"]), count, f"thread {i}")
        return got

    def chain(self, store, lane, ids_of, count):
        """The lane's chain: seq 1 to count, each parent the one before,
        each thread's reported successes in it once, each body naming its
        thread and number once; the entries (line, body) and the thread of
        each, in seq order."""
        lines = scanned(store, scope=SCOPE, lane=lane)
        heads = [json.loads(line) for line in lines]
        self.assertEqual([h["seq"] for h in heads], list(range(1, len(heads) + 1)), lane)
        self.assertEqual(len(heads), count, lane)
        parents = [h.get("parent") for h in heads]
        self.assertEqual(parents, [None] + [h["id"] for h in heads[:-1]], f"{lane}: a fork")
        owner = {}
        for thread, ids in ids_of.items():
            for i in ids:
                self.assertNotIn(i, owner, "a success reported twice")
                owner[i] = thread
        chain_ids = [h["id"] for h in heads]
        self.assertEqual(sorted(chain_ids), sorted(owner), f"{lane}: the chain against the successes")
        entries, threads, numbers = [], [], set()
        for line, h in zip(lines, heads):
            body = store.body(h["id"])
            claim = CLAIM.fullmatch(json.loads(content_text((line, body)))["claim"])
            thread, number = int(claim.group(1)), int(claim.group(2))
            self.assertEqual(thread, owner[h["id"]], "a body naming another thread")
            self.assertNotIn((thread, number), numbers, "a thread's number twice")
            numbers.add((thread, number))
            entries.append((line, body))
            threads.append(thread)
        self.assertEqual(numbers, {(t, n) for t, ids in ids_of.items() for n in range(1, len(ids) + 1)}, lane)
        return entries, threads

    def assert_whole(self, store, entries, lanes):
        """verify passes; the size and count equal the sums recomputed."""
        counts = store.verify()
        self.assertEqual((counts["entries"], counts["bodies"], counts["lanes"]), (len(entries), len(entries), lanes))
        volume = store.size()["volume"]
        size = sum(len(line) + len(body) + 2 for line, body in entries)
        self.assertEqual((volume["size"], volume["count"]), (size, len(entries)))

    def test_S21_m5_threads(self):
        """S21 (M5 with threads): 5 threads, each its own store object,
        started together, each making 69 successful conditional appends of
        one claim on one head, the head read again after each HeadConflict
        or StoreBusy, 5 ms after each success: 345 entries at seq 1 to 345,
        each parent the one before, no fork; each reported success in the
        chain once, each body naming its thread and number; every failure
        HeadConflict or StoreBusy; verify passes; the conflicts above zero,
        the chain's writer changing at least 34 times [proposal]. Needs:
        none; full suite (F127)."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            got = self.race(drive, store, m, ["implementer"] * 5, 69)
            entries, threads = self.chain(store, "implementer", {i: t["ids"] for i, t in enumerate(got)}, 345)
            self.assert_whole(store, entries, 1)
            self.assertGreater(sum(t["conflicts"] for t in got), 0, got)
            changes = sum(1 for a, b in zip(threads, threads[1:]) if a != b)
            self.assertGreaterEqual(changes, M5_CHANGES, f"changes of writer: {changes}")

    def test_S21_m6_threads(self):
        """S21 (M6 with threads): 6 threads append 500 claims each, one a
        call, into 3 lanes each shared by 2, under M5's start, retry and
        pause: 3,000 entries, each thread's number once, read from the
        bodies; each lane one chain of 1,000 with conflicts above zero;
        verify passes; the size and count equal the sums recomputed.
        Needs: none; full suite (F127)."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            lanes = [LANES[i // 2] for i in range(6)]
            got = self.race(drive, store, m, lanes, 500)
            entries = []
            for lane in LANES:
                mine = {i: t["ids"] for i, t in enumerate(got) if lanes[i] == lane}
                entries += self.chain(store, lane, mine, 1000)[0]
                conflicts = sum(got[i]["conflicts"] for i in mine)
                self.assertGreater(conflicts, 0, f"{lane}: {got}")
            self.assert_whole(store, entries, 3)

    def test_S21_other_thread(self):
        """S21: each call on a store object, its scan and its export from
        another thread than their maker's, the export's close among them
        (ruling: exchange's close takes the thread gate), is StoreError
        thread naming the operation, nothing written; each then served on
        its own thread, the export's close after it. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            a = m.claim(SCOPE, "implementer", 1, None, 0)
            store.append(SCOPE, "implementer", None, [a])
            b = m.claim(SCOPE, "implementer", 2, a, 1)
            scan, x = store.scan(), store.export()
            before = snapshot(store)
            on_store = {"store": store.id}
            calls = (
                ("store_append", on_store | {"scope": SCOPE, "lane": "implementer", "expected": id_of(a),
                                             "entries": [entry_wire(b)]}, "append"),
                ("store_head", on_store | {"scope": SCOPE, "lane": "implementer"}, "head"),
                ("store_heads", on_store, "heads"),
                ("store_body", on_store | {"id": id_of(a)}, "body"),
                ("store_purge", on_store, "purge"),
                ("store_verify", on_store, "verify"),
                ("store_close", on_store, "close"),
                ("scan_next", {"scan": scan.id}, "scan"),
                ("export_heads", {"export": x.id}, "export"),
                ("export_next", {"export": x.id}, "export"),
                ("export_close", {"export": x.id}, "close"),
            )
            for op, args, operation in calls:
                with self.subTest(op=op):
                    with self.assertRaises(DriveError) as caught:
                        drive.call(op, elsewhere=True, **args)
                    self.assert_store_error(caught.exception, "StoreError", "thread", operation=operation)
            self.assertIsNone(same_snapshot(before, snapshot(store)))
            self.assertIsNotNone(scan.next())
            self.assertEqual(x.heads()["volume"]["count"], 1)
            self.assertIsNotNone(x.next())
            x.close()
            with self.assertRaises(DriveError) as caught:
                x.next()
            self.assert_store_error(caught.exception, "StoreError", "closed")
            x.close()
            scan.close()
            store.append(SCOPE, "implementer", id_of(a), [b])
            store.close()
