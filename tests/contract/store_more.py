"""The store's cases S10 to S18, each defined once over a store, through
the fake's drive started with --store: mixed into the bindings
(contract/test_store_sqlite.py, contract/test_store_memory.py) beside
store_cases.py's, with its frame (StoreBinding: drive(), needs(), the
typed error's check).

S10, S11 and S16 need files; S16 (c), S17 and S18 are the SQLite store's
own, S16 and S17 forging its file after its close (store_files.py), S18
writing incoherent rows through the seam on its connection. S13 imports
across the two kinds: a second drive, of the other store, over a root
of its own. S12's peak, past the fast group's minute under the thread
sanitizer, is the slow group's (slow/store_peak.py).
"""

import json
from pathlib import Path

from tests.contract.store_entries import (LANES, PERSON, SCOPE, Maker, as_line, canonical, exported, file_state,
                                          fillers, guard_of, header, id_of, lane_heads, scanned, seq_of, snapshot)
from tests.contract import store_files as files
from tests.drive.session import FAKE, Drive, DriveError

# The forbidden plan marks (as S4's plans).
BUILT = ("TEMP B-TREE", "AUTOMATIC", "MATERIALIZE", "BLOOM FILTER")
EXPORT_STATEMENTS = ("forget_counts", "head_rows", "lane_rows")
KIB = 1024
# C15's thresholds, the store's default limits.
DEFAULTS = {"target_size": 512 * KIB * KIB, "warning_size": 640 * KIB * KIB, "max_size": 768 * KIB * KIB,
            "target_count": 400000, "warning_count": 480000, "max_count": 550000}
# S13's refused sources, a case each (test_S13_refused_<source>), so that
# one import of the 3,000 fillers' lines is a case's: two to a case passed
# 30 s on CI's sanitized jobs.
S13_REFUSED = ("forgotten_body", "forgotten_target", "body_missing", "basis")


class StoreMore:
    def other_drive(self):
        """A drive of the other store over a root of its own."""
        root = self.scratch / "other"
        root.mkdir(exist_ok=True)
        other = "memory" if self.STORE == "sqlite" else "sqlite"
        return Drive("--root", str(root), "--store", other, drive=FAKE)

    def assert_corrupt(self, call, code, operation, *args):
        error = self.failure(call, *args)
        self.assert_store_error(error, "StoreCorrupt", code, operation=operation)

    def test_S10_purge_held(self):
        """S10: a second store object holds an export open; a forget's
        purge is pending, with a reason (reader on SQLite), in its
        result and in heads; the export closes; the next append completes
        the purge, and heads gives it done. Needs: files."""
        with self.drive() as drive:
            self.needs(drive, "files")
            own = drive.store_capabilities()["own"] == "sqlite"
            m, store = Maker(drive), drive.store_make()
            a, b, d, acc, e = m.held("A", "B", "D", "ACC", "E")
            store.append(SCOPE, "implementer", None, [a, b, d, acc])
            held = drive.store_again(store.state).export()
            held.heads()
            purge = store.forget(SCOPE, "implementer", id_of(acc), [e], guard_of(store))["purge"]
            self.assertFalse(purge["done"], purge)
            self.assertIn("reason", purge)
            if own:
                self.assertEqual(purge["reason"], "reader")
            self.assertEqual(store.heads()["purge"], purge)
            self.assertIsNone(store.body(id_of(b)))
            held.close()
            written = store.append(SCOPE, "implementer", id_of(e), [m.claim(SCOPE, "implementer", 6, e, 200)])
            self.assertEqual(written["purge"], {"done": True})
            self.assertEqual(store.heads()["purge"], {"done": True})

    def test_S11_raw_byte_scan(self):
        """S11: bodies of 40 bytes and of 20 KB, each holding a marker,
        among 300 rows, both forgotten: after the purge, open and after
        close, no file of the directory holds either marker in UTF-8 or
        UTF-16LE, or either body's key in hex. Before the forget the scan
        finds each. Needs: files."""
        with self.drive() as drive:
            self.needs(drive, "files")
            m, store = Maker(drive), drive.store_make()
            small_marker, large_marker = "<S11-40>", "<S11-20K>"
            small_text = small_marker + " " + "s" * (40 - len(small_marker) - 1)
            self.assertEqual(len(small_text.encode()), 40)
            content = m.content("MX")
            content["responsibility"] = large_marker + " " + content["responsibility"][len(large_marker) + 1:]
            while len(canonical(content).encode()) > 20000:
                cut = content["invariants"].pop()
            content["invariants"].append(cut[:150])
            self.assertTrue(20000 < len(canonical(content).encode()) < 21000, len(canonical(content).encode()))
            lanes = {}
            for lane in ("implementer", "reviewer"):
                lanes[lane] = m.chain(SCOPE, lane, 149)
            small_content = m.content("A")
            small_content["claim"] = small_text
            small = m.on("A", SCOPE, "implementer", 150, lanes["implementer"][-1], 200, small_content, key_of="L1")
            large = m.on("MX", SCOPE, "reviewer", 150, lanes["reviewer"][-1], 201, content)
            for lane, last in (("implementer", small), ("reviewer", large)):
                store.append(SCOPE, lane, None, lanes[lane] + [last])
            self.assertEqual(store.size()["volume"]["count"], 300)
            patterns = (files.marker("small", small_marker) + files.marker("large", large_marker) +
                        files.key("small key", json.loads(small[1])["key"]) +
                        files.key("large key", json.loads(large[1])["key"]))
            _, hits = files.scan(store.directory, patterns)
            found = {h[1] for h in hits}
            self.assertLessEqual({"small utf-8", "large utf-8", "small key hex", "large key hex"}, found, hits)
            first = m.forget(SCOPE, "implementer", 151, small, small, 300)
            forgets = [first, m.forget(SCOPE, "implementer", 152, first, large, 301)]
            written = store.forget(SCOPE, "implementer", id_of(small), forgets, guard_of(store))
            self.assertEqual(sorted(written["erased"]), sorted([id_of(small), id_of(large)]))
            self.assertTrue(written["purge"]["done"], written)
            for when in ("open", "after close"):
                if when == "after close":
                    store.close()
                listed, hits = files.scan(store.directory, patterns)
                with self.subTest(when=when):
                    self.assertTrue(listed)
                    self.assertEqual(hits, [], f"{when}: {listed}")

    def ten_a_lane(self, m, store):
        """10 entries on each of the 3 lanes: each lane's last; all by id."""
        last, held = {}, {}
        for lane in LANES:
            chain = m.chain(SCOPE, lane, 10)
            store.append(SCOPE, lane, None, chain)
            last[lane] = chain[-1]
            held.update({id_of(e): e for e in chain})
        return last, held

    def test_S12_export_during_appends(self):
        """S12: a thread of the drive's, with a store object of its own,
        appends 1,000 entries over 3 lanes in batches of 34: it holds after
        15 while an export's snapshot is read, then appends the rest while
        the export is iterated whole: each snapshot head its lane's greatest
        seq in it, every chain whole, nothing past a head, nothing the
        thread appended after it. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            last, held = self.ten_a_lane(m, store)
            batches, appended, turn = [], 0, 0
            while appended < 1000:
                lane = LANES[turn % 3]
                turn += 1
                batch = m.chain(SCOPE, lane, min(34, 1000 - appended), after=last[lane], minute=seq_of(last[lane]))
                batches.append((SCOPE, lane, id_of(last[lane]), batch))
                last[lane], appended = batch[-1], appended + len(batch)
                held.update({id_of(e): e for e in batch})
            thread = drive.store_appender(store.state, batches, hold=15)
            x = store.export()
            snap = x.heads()
            thread.release()
            got = list(x)
            self.assertEqual(thread.join(), {"appended": 1000})
            x.close()
            self.assertEqual([r["seq"] for r in snap["rows"]], [10 + 5 * 34] * 3)
            self.assert_whole(got, snap["rows"], held)
            self.assertEqual(sum(r["seq"] for r in store.heads()["rows"]), 1030)

    def test_S12_snapshot(self):
        """S12: an export from given seqs, only the lines after them; own:
        sqlite, the export's statements' plans; an export read to its first
        line, then a forget through again of a body later in it, and again
        with the export's heads alone read: the body yielded, forgotten
        false, the snapshot's forget count the old one. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            own = drive.store_capabilities()["own"] == "sqlite"
            m, store = Maker(drive), drive.store_make()
            writer = drive.store_again(store.state)
            _, held = self.ten_a_lane(m, store)
            after = [(SCOPE, lane, 5) for lane in LANES]
            _, lines = exported(store, after=after)
            self.assertEqual(len(lines), 15)
            self.assertEqual([x["line"] for x in lines],
                             [line for line in scanned(store) if seq_of((line, None)) > 5])
            if own:
                planned = {item["name"]: item["lines"] for item in store.plans()}
                for name in EXPORT_STATEMENTS:
                    for line in planned[name]:
                        for bad in BUILT:
                            self.assertNotIn(bad, line, f"{name}: {planned[name]}")
            self.f104(m, store, writer, held)

    def assert_whole(self, got, rows, held):
        """Each snapshot head its lane's greatest seq, every chain whole,
        nothing past a head, each line and body as appended."""
        by_lane = {}
        for line in got:
            h = header((line["line"], None))
            by_lane.setdefault((h["scope"], h["lane"]), []).append(h)
            self.assertEqual((line["line"], line["body"]), held[h["id"]])
            self.assertFalse(line["forgotten"])
        for row in rows:
            chain = by_lane.pop((row["scope"], row["lane"]))
            self.assertEqual([h["seq"] for h in chain], list(range(1, row["seq"] + 1)))
            self.assertEqual(chain[-1]["id"], row["id"])
            for before, h in zip(chain, chain[1:]):
                self.assertEqual(h["parent"], before["id"])
        self.assertEqual(by_lane, {})

    def f104(self, m, store, writer, held):
        """An open export's body kept, twice: a forget through again of a body later in an export,
        once the export has read its first line, then once it has read its
        heads alone (its snapshot fixed from its first read): the body
        yielded, forgotten false, the snapshot's forget count the old one."""
        for seq, minute, line_first in ((2, 900, True), (3, 901, False)):
            x = store.export()
            first = [x.next()] if line_first else []
            forgets = x.heads()["forgets"]
            target = [e for e in held.values() if header(e)["lane"] == "reviewer" and seq_of(e) == seq][0]
            head = writer.head(SCOPE, "reviewer")
            forget = m.forget(SCOPE, "reviewer", head["seq"] + 1, held[head["id"]], target, minute)
            writer.forget(SCOPE, "reviewer", head["id"], [forget], guard_of(writer))
            held[id_of(forget)] = forget
            self.assertIsNone(writer.body(id_of(target)))
            yielded = [line for line in first + list(x) if line["line"] == target[0]]
            self.assertEqual([(y["body"], y["forgotten"]) for y in yielded], [(target[1], False)], line_first)
            self.assertEqual(x.heads()["forgets"], forgets)
            x.close()

    def source(self, m, store):
        """S13's source: 2 scopes of 3 lanes; a check, a retire and a
        forget whose targets are in lanes that sort later; forgotten
        bodies, their purge done, one a record of two versions forgotten
        whole."""
        for lane in LANES:
            store.append(PERSON, lane, None, m.chain(PERSON, lane, 2))
        r1, r2 = m.chain(SCOPE, "reviewer", 2)
        store.append(SCOPE, "reviewer", None, [r1, r2])
        l1, l2 = m.held("L1", "L2")
        c3 = m.claim(SCOPE, "learner", 3, l2, 110)
        store.append(SCOPE, "learner", None, [l1, l2, c3])
        i1 = m.claim(SCOPE, "implementer", 1, None, 111)
        check = m.on("C", SCOPE, "implementer", 2, i1, 112, target=id_of(r1))
        retired = {"reason": "Replaced by a later claim.", **{k: m.content("C")[k] for k in ("episode", "evidence")}}
        retire = m.on("C", SCOPE, "implementer", 3, check, 113, retired, kind="retire", target=id_of(r2),
                      basis="claimed")
        store.append(SCOPE, "implementer", None, [i1, check, retire])
        f = m.forget(SCOPE, "implementer", 4, retire, c3, 114)
        self.assertTrue(store.forget(SCOPE, "implementer", id_of(retire), [f], guard_of(store))["purge"]["done"])
        f1 = m.forget(SCOPE, "learner", 4, c3, l1, 115)
        f2 = m.forget(SCOPE, "learner", 5, f1, l2, 116)
        written = store.forget(SCOPE, "learner", id_of(c3), [f1, f2], guard_of(store))
        self.assertEqual(sorted(written["erased"]), sorted([id_of(l1), id_of(l2)]))
        self.assertTrue(written["purge"]["done"])

    def assert_same(self, source, target, what):
        """target holds what source does: ids, heads, header lines, held
        bodies, the forgotten set, size, count and forget count; verify
        passes on it."""
        (sh, sl), (th, tl) = exported(source), exported(target)
        self.assertEqual(tl, sl, what)
        self.assertEqual(th["rows"], sh["rows"], what)
        self.assertEqual(th["forgets"], sh["forgets"], what)
        self.assertEqual(target.size()["volume"], source.size()["volume"], what)
        self.assertEqual(target.heads(), source.heads(), what)
        self.assertEqual(target.verify(), source.verify(), what)

    def test_S13_round_trip(self):
        """S13: the source's export imported into an empty store of its own
        kind and of the other: everything equal, verify passing. The base
        chain's half is not package 5's. Needs: none."""
        with self.drive() as drive, self.other_drive() as other:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            self.source(m, store)
            self.assertGreater(len([x for x in exported(store)[1] if x["forgotten"]]), 2)
            into = drive.store_make()
            x = store.export()
            counts = into.import_(x)
            x.drop()
            self.assertEqual(counts["counts"]["entries"], store.size()["volume"]["count"])
            self.assert_same(store, into, "into its own kind")
            heads, lines = exported(store)
            across = other.store_make()
            across.import_((heads["rows"], lines))
            (oh, ol), (sh, sl) = exported(across), (heads, lines)
            self.assertEqual(ol, sl)
            self.assertEqual((oh["rows"], oh["forgets"]), (sh["rows"], sh["forgets"]))
            self.assertEqual(across.size()["volume"], store.size()["volume"])
            self.assertEqual(across.verify(), store.verify())

    def refused(self, code):
        """S13's last four, the source of code (S13_REFUSED), a case each:
        a source holding a body its own forget targets; a revision, not
        forgotten, of a forgotten version; a line lacking its body record;
        a forgotten check of basis derived; each after 3,000 entries
        of 1 KiB, which spill pages into the -wal: EntryInvalid
        forgotten_body, forgotten_target, body_missing, basis, nothing
        written, the -wal included. The class holds a case for each source
        of S13_REFUSED and no other, and the sources are those four."""
        held = sorted(name for name in dir(type(self)) if name.startswith("test_S13_refused_"))
        self.assertEqual(held, sorted(f"test_S13_refused_{c}" for c in S13_REFUSED))
        with self.drive() as drive:
            self.needs(drive)
            m = Maker(drive)
            filler = [as_line(e) for e in fillers(m, PERSON, "implementer", 3000)]
            a1 = m.claim(SCOPE, "implementer", 1, None, 100)
            b1 = m.claim(SCOPE, "implementer", 2, a1, 101)
            l1, l2 = m.held("L1", "L2")
            check = m.on("C", SCOPE, "implementer", 2, a1, 101, target=id_of(a1), basis="derived")
            tails = (
                ("forgotten_body", [as_line(a1), as_line(b1), as_line(m.forget(SCOPE, "implementer", 3, b1, b1, 102))]),
                ("forgotten_target", [as_line(l1, True), as_line(l2), as_line(m.forget(SCOPE, "learner", 3, l2, l1,
                                                                                      120))]),
                ("body_missing", [as_line(a1, body=False)]),
                ("basis", [as_line(a1), as_line(check, True),
                           as_line(m.forget(SCOPE, "implementer", 3, check, check, 102))]),
            )
            self.assertEqual([t[0] for t in tails], list(S13_REFUSED))
            tail = dict(tails)[code]
            store = drive.store_make()
            lines = filler + sorted(tail, key=lambda x: (header((x["line"], None))["lane"],
                                                         header((x["line"], None))["seq"]))
            before = snapshot(store)
            error = self.failure(store.import_, (lane_heads(lines), lines))
            self.assert_store_error(error, "EntryInvalid", code)
            self.nothing_written(store, before, code)
            if store.path is not None:
                self.assertIn((file_state(store.path + "-wal") or (0,))[0], (0,))

    def test_S14_import_not_empty(self):
        """S14: an import into a store holding an entry: StoreNotEmpty;
        nothing written. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m = Maker(drive)
            a, b = m.held("A", "B")
            source, store = drive.store_make(), drive.store_make()
            source.append(SCOPE, "implementer", None, [a, b])
            store.append(SCOPE, "implementer", None, [a])
            before = snapshot(store)
            x = source.export()
            error = self.failure(store.import_, x)
            self.assert_store_error(error, "StoreNotEmpty", count=1)
            self.nothing_written(store, before, "the import")
            heads, lines = exported(source)
            error = self.failure(store.import_, (heads["rows"], lines))
            self.assert_store_error(error, "StoreNotEmpty")
            self.nothing_written(store, before, "the given import")

    def climb(self, drive, m, limits):
        """A store of these limits, appended one claim at a time to the
        maximum: each volume and its flags as the limits give them; the
        append past the maximum StoreFull store_full, naming its size and
        count, nothing written. The store and its last entry."""
        bound = {**DEFAULTS, **limits}
        store = drive.store_make(limits)
        self.assertEqual(store.limits(), bound)
        flagged, parent, volume = set(), None, store.size()["volume"]
        for i in range(400):
            e = m.claim(SCOPE, "implementer", i + 1, parent, i)
            size, count = volume["size"] + len(e[0]) + len(e[1]) + 2, volume["count"] + 1
            if size > bound["max_size"] or count > bound["max_count"]:
                before = snapshot(store)
                error = self.failure(store.append, SCOPE, "implementer", id_of(parent), [e])
                self.assert_store_error(error, "StoreFull", "store_full", size=size, count=count,
                                        max_size=bound["max_size"], max_count=bound["max_count"])
                self.nothing_written(store, before, "past the maximum")
                self.assertEqual(flagged, {"over_target", "over_warning"})
                return store, parent
            volume = store.append(SCOPE, "implementer", None if parent is None else id_of(parent), [e])["volume"]
            self.assertEqual((volume["size"], volume["count"]), (size, count))
            for flag, at in (("over_target", "target"), ("over_warning", "warning")):
                expected = size > bound[at + "_size"] or count > bound[at + "_count"]
                self.assertEqual(volume[flag], expected, (flag, volume))
                if expected:
                    flagged.add(flag)
            parent = e
        raise AssertionError("the maximum never reached")

    def test_S15_sizes(self):
        """S15: limits of 64, 96 and 128 KiB, then of 10, 15 and 20
        entries: over_target, then over_warning, by size and by count
        apart; past the maximum StoreFull store_full, nothing written; a
        forget accepted past either maximum; an import past it
        accepted, with its flags; the default limits C15's. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m = Maker(drive)
            self.assertEqual(drive.store_make().limits(), DEFAULTS)
            axes = (("size", {"target_size": 64 * KIB, "warning_size": 96 * KIB, "max_size": 128 * KIB}),
                    ("count", {"target_count": 10, "warning_count": 15, "max_count": 20}))
            for axis, limits in axes:
                with self.subTest(by=axis):
                    store, last = self.climb(drive, m, limits)
                    first = store.head(SCOPE, "implementer")
                    target = m.claim(SCOPE, "implementer", 1, None, 0)
                    forget = m.forget(SCOPE, "implementer", first["seq"] + 1, last, target, 500)
                    volume = store.forget(SCOPE, "implementer", first["id"], [forget], guard_of(store))["volume"]
                    self.assertTrue(volume["over_warning"], volume)
                    if axis == "count":
                        self.assertGreater(volume["count"], limits["max_count"])
                    source = drive.store_make()
                    chain = m.chain(SCOPE, "implementer", 140 if axis == "size" else 25)
                    source.append(SCOPE, "implementer", None, chain)
                    into = drive.store_make(limits)
                    x = source.export()
                    volume = into.import_(x)["volume"]
                    x.drop()
                    self.assertEqual(volume, {**source.size()["volume"], "over_target": True, "over_warning": True})
                    bound = {**DEFAULTS, **limits}
                    self.assertTrue(volume["size"] > bound["max_size"] or volume["count"] > bound["max_count"])
                    forget = m.forget(SCOPE, "implementer", len(chain) + 1, chain[-1], chain[0], 600)
                    into.forget(SCOPE, "implementer", id_of(chain[-1]), [forget], guard_of(into))
                    error = self.failure(into.append, SCOPE, "implementer", id_of(forget),
                                         [m.claim(SCOPE, "implementer", len(chain) + 2, forget, 601)])
                    self.assert_store_error(error, "StoreFull", "store_full")

    def closed(self, drive, entries):
        """A SQLite store holding entries on the implementer's lane, closed:
        the object, its file's path and bytes."""
        store = drive.store_make()
        store.append(SCOPE, "implementer", None, entries)
        store.close()
        path = Path(store.path)
        self.assertEqual(files.side_files(path), [])
        return store, path, path.read_bytes()

    def test_S16_corrupt_file(self):
        """S16: after close, (a) a hex digit of a header line changed in
        place: StoreCorrupt id_mismatch at header, scan and verify; (b) a
        byte of a body's content: body_mismatch at body and verify; (c) a
        b-tree page's type byte, (d) the file cut to half its length:
        StoreCorrupt at open or at verify; never a wrong value. Needs:
        files; (c) own: sqlite."""
        with self.drive() as drive:
            self.needs(drive, "files")
            own = drive.store_capabilities()["own"] == "sqlite"
            m = Maker(drive)
            a, b, d = m.held("A", "B", "D")
            with self.subTest(forged="(a) a header line's digit"):
                store, path, data = self.closed(drive, [a, b, d])
                at = b[0].index(b'"content":"hmac-sha256:') + len(b'"content":"hmac-sha256:')
                path.write_bytes(files.changed_once(data, b[0], files.digit_changed(b[0], at)))
                back = drive.store_reopen(store.state)
                self.assertEqual(back.header(id_of(a)), a[0])
                self.assert_corrupt(back.header, "id_mismatch", "header", id_of(b))
                scan = back.scan()
                self.assertEqual(scan.next(), a[0])
                self.assert_corrupt(scan.next, "id_mismatch", "scan")
                scan.close()
                self.assert_corrupt(back.verify, "id_mismatch", "verify")
            with self.subTest(forged="(b) a body's byte"):
                store, path, data = self.closed(drive, [a, b, d])
                at = b[1].index(b'"claim":"') + len(b'"claim":"')
                record = b[1][:at] + (b"Q" if b[1][at:at + 1] != b"Q" else b"R") + b[1][at + 1:]
                path.write_bytes(files.changed_once(data, b[1], record))
                back = drive.store_reopen(store.state)
                self.assertEqual(back.body(id_of(a)), a[1])
                self.assert_corrupt(back.body, "body_mismatch", "body", id_of(b))
                self.assert_corrupt(back.verify, "body_mismatch", "verify")
            forged = [("(d) cut to half", lambda data: data[:len(data) // 2])]
            if own:
                forged.insert(0, ("(c) a page's type byte", lambda data: files.type_byte_changed(data)[0]))
            for what, forge in forged:
                with self.subTest(forged=what):
                    store, path, data = self.closed(drive, [a, b, d])
                    path.write_bytes(forge(data))
                    try:
                        back = drive.store_reopen(store.state)
                    except DriveError as error:
                        self.assert_store_error(error, "StoreCorrupt", operation="open")
                        continue
                    self.assert_store_error(self.failure(back.verify), "StoreCorrupt", operation="verify")

    def test_S17_foreign_file(self):
        """S17: a text file; a store file with application_id 0 and
        another; with user_version 2; another application's file in DELETE
        mode, and one with no table; a 0-byte file: at open StoreCorrupt
        not_a_store, foreign, foreign, version, foreign, foreign, corrupt;
        create over each and over a store, StoreError exists; each file
        byte-equal after, no -wal or -shm left. Needs: own:
        sqlite."""
        with self.drive() as drive:
            self.needs(drive, "own: sqlite")
            m = Maker(drive)
            a = m.fixture("A")
            store, path, good = self.closed(drive, [a])
            ours = int.from_bytes(good[files.APPLICATION_ID_AT:files.APPLICATION_ID_AT + 4], "big")
            forged = (("a text file", b"Not a database: a line of text.\n" * 128, "not_a_store"),
                      ("application_id 0", files.patched(good, files.APPLICATION_ID_AT, 0), "foreign"),
                      ("another application_id", files.patched(good, files.APPLICATION_ID_AT, ours ^ 1), "foreign"),
                      ("user_version 2", files.patched(good, files.USER_VERSION_AT, 2), "version"),
                      ("another application's, DELETE mode", files.delete_mode(good, 0x0A0B0C0D), "foreign"),
                      ("no table", files.table_less(), "foreign"),
                      ("0 bytes", b"", "corrupt"))
            for what, data, code in forged:
                with self.subTest(file=what):
                    state, at, _ = self.closed(drive, [a])
                    at.write_bytes(data)
                    error = self.failure(drive.store_reopen, state.state)
                    self.assert_store_error(error, "StoreCorrupt", code)
                    self.assertEqual((at.read_bytes(), files.side_files(at)), (data, []))
                    error = self.failure(drive.store_create, state.state)
                    self.assert_store_error(error, "StoreError", "exists")
                    self.assertEqual((at.read_bytes(), files.side_files(at)), (data, []))
            error = self.failure(drive.store_create, store.state)
            self.assert_store_error(error, "StoreError", "exists")
            self.assertEqual((path.read_bytes(), files.side_files(path)), (good, []))

    def test_S18_incoherent(self):
        """S18: through the seam, on a store holding a forget: a head row
        set to an older entry; a body row put back for a forgotten id;
        purged above vacuumed; a body row deleted: at open
        StoreCorrupt incoherent, forgotten_body, incoherent, incoherent,
        the last two body rows also at verify; and meta's size off by one,
        incoherent at verify. The count rule's cost at 555,000 entries is
        the slow group's. Needs: own: sqlite."""
        with self.drive() as drive:
            self.needs(drive, "own: sqlite")
            m = Maker(drive)
            a, b, c, d, acc, e = m.held("A", "B", "C", "D", "ACC", "E")
            rows = (
                ("an older head", f"UPDATE head SET id = '{id_of(a)}', seq = 1 WHERE scope = '{SCOPE}' AND "
                                  "lane = 'implementer'", "incoherent", None),
                ("a forgotten body back", f"INSERT INTO body (id, record) VALUES ('{id_of(b)}', X'{b[1].hex()}')",
                 "forgotten_body", "forgotten_body"),
                ("purged above vacuumed", PURGED_ABOVE_VACUUMED, "incoherent", None),
                ("a body deleted", f"DELETE FROM body WHERE id = '{id_of(d)}'", "incoherent", "incoherent"),
                ("meta's size", "UPDATE meta SET value = value + 1 WHERE name = 'size'", None, "incoherent"),
            )
            for what, sql, at_open, at_verify in rows:
                with self.subTest(row=what):
                    store = drive.store_make()
                    store.append(SCOPE, "implementer", None, [a, b, d, acc])
                    store.append(SCOPE, "reviewer", None, [c])
                    store.forget(SCOPE, "implementer", id_of(acc), [e], guard_of(store))
                    self.assertEqual(store.verify()["entries"], 6)
                    self.assertEqual(store.seam_write(sql), 0)
                    if at_verify is not None:
                        self.assert_corrupt(store.verify, at_verify, "verify")
                    store.close()
                    if at_open is not None:
                        error = self.failure(drive.store_reopen, store.state)
                        self.assert_store_error(error, "StoreCorrupt", at_open, operation="open")


# The seam's row of S18 (support/seams' purged_above_vacuumed).
PURGED_ABOVE_VACUUMED = ("UPDATE meta SET value = (SELECT value FROM meta WHERE name = 'vacuumed') + 1 "
                         "WHERE name = 'purged'")


def s13_case(code):
    """S13's refused source code as a case of StoreMore's, named and
    described."""
    def case(self):
        self.refused(code)
    case.__name__ = f"test_S13_refused_{code}"
    case.__doc__ = f"S13's last four, the source {code}: as StoreMore.refused, EntryInvalid {code}. Needs: none."
    return case


for _code in S13_REFUSED:
    setattr(StoreMore, f"test_S13_refused_{_code}", s13_case(_code))
