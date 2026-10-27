"""The store's cases S19 to S28 but S21, each defined once over a store,
through the fake's drive started with --store: mixed into the bindings
(contract/test_store_sqlite.py, contract/test_store_memory.py) beside
store_cases.py's and store_more.py's, with their frame (StoreBinding:
drive(), needs(), the typed error's check; StoreCases' nothing_written).

S19, S24 and S28 are the SQLite store's own: its settings read back on its
connection, an import through the seam that does not defer foreign keys,
and a create refused call by call through the drive's countdowns, in parts
(S28_PARTS). S22
needs files: the guard on a path in a checkout and in a folder the Site's
fake reports synced. S21, with its threads, is the slow group's.
"""

import shutil
import unittest

from tests.contract.store_entries import (CLEARING, LANES, PERSON, SCOPE, Maker, at, clearing, exported, guard_of,
                                          header, id_of, scanned, snapshot)
from tests.drive.session import has_faults

# The text's settings table, as the engine reads each back: application_id
# 0x71763301 as a number.
TEXT_SETTINGS = (("journal_mode", "wal"), ("synchronous", "2"), ("secure_delete", "1"), ("temp_store", "2"),
                 ("foreign_keys", "1"), ("trusted_schema", "0"), ("cell_size_check", "1"), ("auto_vacuum", "0"),
                 ("busy_timeout", "5000"), ("application_id", str(0x71763301)), ("user_version", "1"))
# The countdowns of a create, and the removals a refused one makes, in the
# engine calls' order after open_v2's.
KINDS = ("platform", "arena", "engine_call", "engine_heap")
REMOVALS = ("-journal", "-wal", "-shm", "the file")
TYPED = ("store", "memory", "platform")
# S28's refusals in parts, test_S28_refused_part1 on, each within 30 s on
# every job, as store_sqlite.create_refused's: each part's first point, a
# pass of KINDS by its place and its k; a part runs the points from its own
# up to the next part's, the last to its pass's completing create. The
# platform's, the arena's and the engine calls' to k 23; the engine calls'
# from k 24, 48 and 71; the engine heap's 25 k a part: half the stretches
# a CI Windows job ran in up to 28.2 s, so that each part takes 20 s or
# under there.
S28_PARTS = ((0, 1), (2, 24), (2, 48), (2, 71), *((3, k) for k in range(1, 627, 25)))


def s28_range(part, kind):
    """The k part (from 0) runs of pass kind: (first, last), last None to
    the completing create; None when it runs none."""
    (start_pass, start_k), end = S28_PARTS[part], S28_PARTS[part + 1] if part + 1 < len(S28_PARTS) else None
    if kind < start_pass or (end is not None and (kind > end[0] or (kind == end[0] and end[1] == 1))):
        return None
    return (start_k if kind == start_pass else 1, end[1] - 1 if end is not None and kind == end[0] else None)


def error_of(reply):
    """store_open_at's error: the store's typed error, or the one the
    drive's reply holds."""
    error = reply.get("error")
    return error["error"] if error is not None and "ok" in error else error


class StoreRest:
    def site_root(self):
        """The Site's root of this case's drive (its --root's stores/)."""
        return self.scratch / "root" / "stores"

    def test_S19_settings(self):
        """S19: open, read-write and read-only: each setting reads back its
        value; SETTINGS equals the text's table. Needs: own: sqlite."""
        with self.drive() as drive:
            self.needs(drive, "own: sqlite")
            store = drive.store_make()
            store.append(SCOPE, "implementer", None, Maker(drive).chain(SCOPE, "implementer", 1))
            for what, obj in (("read-write", drive.store_again(store.state)),
                              ("read-only", drive.store_read_only(store.state)), ("made", store)):
                with self.subTest(open=what):
                    got = obj.settings()
                    self.assertEqual([(s["name"], s["value"]) for s in got], list(TEXT_SETTINGS))
                    for s in got:
                        self.assertEqual(s.get("read"), s["value"], f"{what}: {s}")

    def test_S20_closed(self):
        """S20: close, then each operation, a scan and an export begun
        before: StoreError closed, on both stores (a scan is read only
        while its store object is open). Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            entries = m.chain(SCOPE, "implementer", 3)
            store.append(SCOPE, "implementer", None, entries[:2])
            first = id_of(entries[0])
            scan, export = store.scan(), store.export()
            self.assertEqual(scan.next(), entries[0][0])
            source = drive.store_make()
            source.append(SCOPE, "implementer", None, entries[:1])
            given = exported(source)
            store.close()
            calls = {
                "append": lambda: store.append(SCOPE, "implementer", id_of(entries[1]), entries[2:]),
                "head": lambda: store.head(SCOPE, "implementer"), "heads": store.heads,
                "header": lambda: store.header(first), "body": lambda: store.body(first),
                "forgotten": lambda: store.forgotten(first), "scan": store.scan,
                "forget": lambda: store.forget(SCOPE, "implementer", id_of(entries[1]),
                                               [m.forget(SCOPE, "implementer", 3, entries[1], entries[1], 9)],
                                               guard_of(source)),
                "purge": store.purge, "export": store.export,
                "import": lambda: store.import_((given[0]["rows"], given[1])), "size": store.size,
                "verify": store.verify, "close": store.close, "scan_next": scan.next, "export_heads": export.heads,
                "export_next": export.next,
            }
            for what, call in calls.items():
                with self.subTest(operation=what):
                    self.assert_store_error(self.failure(call), "StoreError", "closed")
            scan.close()
            export.drop()
            again = drive.store_again(store.state)
            self.assertEqual(again.head(SCOPE, "implementer")["seq"], 2)

    def test_S22_location(self):
        """S22: a path in a directory holding .git, and one in a folder the
        platform reports synced (%OneDrive%'s): StoreError location; no
        file made. A path outside both: made. Needs: files."""
        with self.drive() as drive:
            self.needs(drive, "files")
            root = self.site_root()
            (root / "repo" / ".git").mkdir(parents=True)
            (root / "OneDrive").mkdir()
            for path, detail in (("repo/work/store.db", "checkout"), ("repo/store.db", "checkout"),
                                 ("OneDrive/context/store.db", "synced")):
                for create in (True, False):
                    with self.subTest(path=path, create=create):
                        got = error_of(drive.store_open_at(path, create=create))
                        self.assertIsNotNone(got, path)
                        self.assertEqual((got["class"], got["code"], got["detail"]),
                                         ("StoreError", "location", detail), got)
            self.assertEqual(sorted(p.name for p in (root / "repo").iterdir()), [".git"])
            self.assertEqual(list((root / "repo" / ".git").iterdir()), [])
            self.assertEqual(list((root / "OneDrive").iterdir()), [])
            self.assertTrue(drive.store_open_at("plain/store.db", create=True)["made"])
            self.assertTrue((root / "plain" / "store.db").exists())

    def test_S23_read_only(self):
        """S23: read_only over a store with entries: each read as S2; each
        write StoreError read_only, nothing written by it; again appends,
        the append seen at the next read. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            made = {lane: m.chain(SCOPE, lane, 3) for lane in LANES}
            for lane, entries in made.items():
                store.append(SCOPE, lane, None, entries)
            reader = drive.store_read_only(store.state)
            self.assertTrue(reader.read_only())
            for lane, entries in made.items():
                self.assertEqual(reader.head(SCOPE, lane), {"id": id_of(entries[-1]), "seq": 3})
                for entry in entries:
                    self.assertEqual(reader.header(id_of(entry)), entry[0])
                    self.assertEqual(reader.body(id_of(entry)), entry[1])
                    self.assertFalse(reader.forgotten(id_of(entry)))
            self.assertEqual(reader.heads(), store.heads())
            self.assertEqual(scanned(reader), scanned(store))
            self.assertEqual(exported(reader), exported(store))
            self.assertEqual(reader.size()["volume"], store.size()["volume"])
            self.assertEqual(reader.verify(), store.verify())
            given = exported(store)
            last = made["implementer"][-1]
            writes = {
                "append": lambda: reader.append(SCOPE, "implementer", id_of(last),
                                                [m.claim(SCOPE, "implementer", 4, last, 10)]),
                "forget": lambda: reader.forget(SCOPE, "implementer", id_of(last),
                                                [m.forget(SCOPE, "implementer", 4, last, last, 10)], guard_of(store)),
                "purge": reader.purge,
                "import": lambda: reader.import_((given[0]["rows"], given[1])),
            }
            for what, call in writes.items():
                with self.subTest(write=what):
                    before = snapshot(store)
                    self.assert_store_error(self.failure(call), "StoreError", "read_only")
                    self.nothing_written(store, before, what)
            again = drive.store_again(store.state)
            more = m.claim(SCOPE, "implementer", 4, last, 11)
            again.append(SCOPE, "implementer", id_of(last), [more])
            self.assertEqual(reader.head(SCOPE, "implementer"), {"id": id_of(more), "seq": 4})
            self.assertEqual(reader.header(id_of(more)), more[0])
            reader.close()

    def plain_source(self, m, store):
        """S13's source without its cross-lane changes: each lane's entries,
        the learner's forgets within its lane, nothing naming a lane that
        sorts later."""
        for lane in LANES:
            store.append(PERSON, lane, None, m.chain(PERSON, lane, 2))
        store.append(SCOPE, "reviewer", None, m.chain(SCOPE, "reviewer", 2))
        l1, l2 = m.held("L1", "L2")
        c3 = m.claim(SCOPE, "learner", 3, l2, 110)
        store.append(SCOPE, "learner", None, [l1, l2, c3])
        store.append(SCOPE, "implementer", None, [m.claim(SCOPE, "implementer", 1, None, 111)])
        f1 = m.forget(SCOPE, "learner", 4, c3, l1, 115)
        f2 = m.forget(SCOPE, "learner", 5, f1, l2, 116)
        self.assertTrue(store.forget(SCOPE, "learner", id_of(c3), [f1, f2], guard_of(store))["purge"]["done"])

    def test_S24_import_undeferred(self):
        """S24: S13's source imported through the seam that does not defer
        foreign keys: StoreError guard, detail 787, nothing written; the
        same source without its cross-lane changes, imported (S13 a near
        miss that only the deferral passes, F28). Needs: own: sqlite."""
        with self.drive() as drive:
            self.needs(drive, "own: sqlite")
            m, store = Maker(drive), drive.store_make()
            self.source(m, store)
            into = drive.store_make()
            before = snapshot(into)
            export = store.export()
            error = self.failure(drive.store_import_undeferred, into.state, export)
            export.drop()
            self.assert_store_error(error, "StoreError", "guard")
            self.assertIn("787", error.detail, error.error)
            self.nothing_written(into, before, "the undeferred import")
            plain = drive.store_make()
            self.plain_source(m, plain)
            heads, lines = exported(plain)
            counts = drive.store_import_undeferred(into.state, (heads["rows"], lines))
            self.assertEqual(counts["counts"]["entries"], plain.size()["volume"]["count"])
            self.assertEqual(exported(into), (heads, lines))
            self.assertEqual(into.verify(), plain.verify())
            control = drive.store_make()
            export = store.export()
            control.import_(export)
            export.drop()
            self.assertEqual(exported(control), exported(store))

    def lesson(self, m, lane, seq, parent, minute, target=None):
        """A lesson like L1 on lane, a revision of target where given."""
        content = m.content("L1")
        content["lesson"] = f"Lesson {seq} of {lane} at minute {minute}."
        fields = {} if target is None else {"target": id_of(target)}
        return m.on("L1", SCOPE, lane, seq, parent, minute, content, **fields)

    def forgets(self, m, lane, parent, targets, minute):
        """One forget a target on lane after parent, each a minute on."""
        out, seq = [], header(parent)["seq"]
        for i, target in enumerate(targets):
            parent = m.forget(SCOPE, lane, seq + i + 1, parent, target, minute + i)
            out.append(parent)
        return out

    def test_S25_whole_records(self):
        """S25 (F174): (a) v1 and its revision v2 in one lane: forget v2,
        then v1, each admitted; (b) a revision in another lane: forget v1,
        whole_record; (c) v3 in another lane revising v2: {v1, v2}
        whole_record, then v3, then {v1, v2} admitted; (d) a fork, v2a in
        the lane and v2b in another: {v1, v2a} whole_record, then {v1, v2a,
        v2b} admitted. Each refusal writes nothing, the store then opening
        coherent; verify, an append's rule apart, reports no
        forgotten_target. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m = Maker(drive)
            cases = {}
            v1 = self.lesson(m, "learner", 1, None, 10)
            v2 = self.lesson(m, "learner", 2, v1, 11, target=v1)
            cases["a"] = ({"learner": [v1, v2]}, [[v2], [v1]], [None, None])
            w2 = self.lesson(m, "implementer", 1, None, 11, target=v1)
            cases["b"] = ({"learner": [v1], "implementer": [w2]}, [[v1]], ["whole_record"])
            v3 = self.lesson(m, "implementer", 1, None, 12, target=v2)
            cases["c"] = ({"learner": [v1, v2], "implementer": [v3]}, [[v1, v2], [v3], [v1, v2]],
                          ["whole_record", None, None])
            v2b = self.lesson(m, "implementer", 1, None, 12, target=v1)
            cases["d"] = ({"learner": [v1, v2], "implementer": [v2b]}, [[v1, v2], [v1, v2, v2b]],
                          ["whole_record", None])
            for name, (lanes, steps, outcomes) in cases.items():
                with self.subTest(case=name):
                    store = drive.store_make()
                    for lane, entries in lanes.items():
                        store.append(SCOPE, lane, None, entries)
                    held = {id_of(e): e for es in lanes.values() for e in es}
                    minute = 100
                    for targets, outcome in zip(steps, outcomes):
                        lane = header(targets[0])["lane"]
                        head = store.head(SCOPE, lane)["id"]
                        entries = self.forgets(m, lane, held[head], targets, minute)
                        minute += 10
                        if outcome is None:
                            written = store.forget(SCOPE, lane, head, entries, guard_of(store))
                            self.assertEqual(sorted(written["erased"]), sorted(id_of(t) for t in targets))
                            held.update({id_of(e): e for e in entries})
                        else:
                            before = snapshot(store)
                            error = self.failure(store.forget, SCOPE, lane, head, entries, guard_of(store))
                            self.assert_store_error(error, "StoreError", outcome)
                            self.nothing_written(store, before, f"({name}) {outcome}")
                    opened = drive.store_again(store.state)
                    self.assertEqual(opened.verify(), store.verify())
                    for target in steps[-1] if outcomes[-1] is None else ():
                        self.assertTrue(store.forgotten(id_of(target)), name)
                        self.assertIsNone(store.body(id_of(target)), name)

    def direction(self, m, content, scope=SCOPE, seq=1, parent=None, minute=60, **fields):
        """D's direction on scope's reviewer lane, its content given."""
        return m.on("D", scope, "reviewer", seq, parent, minute, content, **fields)

    def test_S26_clearing_conditions(self):
        """S26 (F173, F174), on the first clearing fixture: a direction of
        another scope, a revised one whose current version has no clears, a
        retired one, a forgotten one, and one expiring at or before the
        retry's at each leave the retry secret; a revision keeping clears,
        or an expiry after the retry's at, admits it. The way out: words
        quoting the value refused secret at words; words naming the digest
        admitted, clearing it. Nothing written by a refusal. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m = Maker(drive)
            value, pattern = CLEARING[0]
            hit = m.claim(SCOPE, "implementer", 1, None, 240, mistake=value)
            digest = header(hit)["content"]
            clears = clearing(m, digest, pattern, 1, None, 60)
            cleared = m.content("D")
            cleared["clears"] = {"content": digest, "pattern": pattern}
            retired = {"reason": "Withdrawn by the owner.", **{k: m.content("C")[k] for k in ("episode", "evidence")}}

            def expiring(minute):
                return self.direction(m, {**cleared, "expires": at(minute)})

            conditions = {
                "another scope": ([self.direction(m, cleared, scope=PERSON)], None, False),
                "revised without clears": ([clears, self.direction(m, m.content("D"), seq=2, parent=clears, minute=61,
                                                                   target=id_of(clears))], None, False),
                "retired": ([clears, m.on("C", SCOPE, "reviewer", 2, clears, 61, retired, kind="retire",
                                          target=id_of(clears), basis="claimed")], None, False),
                "forgotten": ([clears], m.forget(SCOPE, "reviewer", 2, clears, clears, 61), False),
                "expired at the retry": ([expiring(240)], None, False),
                "expired before": ([expiring(239)], None, False),
                "revision keeping clears": ([clears, self.direction(m, {**cleared, "words": "Still not a password."},
                                                                    seq=2, parent=clears, minute=61,
                                                                    target=id_of(clears))], None, True),
                "expiring after": ([expiring(241)], None, True),
            }
            for what, (directions, forget, admitted) in conditions.items():
                with self.subTest(condition=what):
                    store = drive.store_make()
                    self.refuse(store, SCOPE, "implementer", None, [hit], "EntryInvalid", "secret", pattern=pattern)
                    scope = header(directions[0])["scope"]
                    store.append(scope, "reviewer", None, directions)
                    if forget is not None:
                        written = store.forget(SCOPE, "reviewer", id_of(directions[-1]), [forget], guard_of(store))
                        self.assertEqual(written["erased"], [id_of(directions[0])])
                    if admitted:
                        self.assertEqual(store.append(SCOPE, "implementer", None, [hit])["head"]["seq"], 1)
                    else:
                        self.refuse(store, SCOPE, "implementer", None, [hit], "EntryInvalid", "secret",
                                    pattern=pattern)
            store = drive.store_make()
            quoting = self.direction(m, {**cleared, "words": value + " is a path, not a password"})
            self.refuse(store, SCOPE, "reviewer", None, [quoting], "EntryInvalid", "secret", pattern=pattern,
                        field="words")
            naming = self.direction(m, {**cleared, "words": digest + " trips " + pattern + ": a path, not a password"})
            store.append(SCOPE, "reviewer", None, [naming])
            self.assertEqual(store.append(SCOPE, "implementer", None, [hit])["head"]["seq"], 1)

    def test_S27_scan_end(self):
        """S27: three scans begun (every lane, one lane, after a seq), one
        header read from the first; then a forget, its purge done, a new
        lane through again, an append through the store object: each scan
        gives every entry committed before it began, each once, none after;
        one begun after gives all; again's own sees the append. Needs:
        none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            made = {lane: m.chain(SCOPE, lane, 3) for lane in ("implementer", "reviewer")}
            for lane, entries in made.items():
                store.append(SCOPE, lane, None, entries)
            every = [e[0] for lane in ("implementer", "reviewer") for e in made[lane]]
            whole, one, after = store.scan(), store.scan(SCOPE, "reviewer"), store.scan(SCOPE, "implementer", 1)
            first = whole.next()
            last = made["implementer"][-1]
            gone = m.forget(SCOPE, "implementer", 4, last, made["implementer"][0], 20)
            self.assertTrue(store.forget(SCOPE, "implementer", id_of(last), [gone], guard_of(store))["purge"]["done"])
            again = drive.store_again(store.state)
            lane = m.chain(SCOPE, "learner", 2, minute=21)
            again.append(SCOPE, "learner", None, lane)
            tail = made["reviewer"][-1]
            more = m.claim(SCOPE, "reviewer", 4, tail, 30)
            store.append(SCOPE, "reviewer", id_of(tail), [more])
            self.assertEqual([first, *whole], every)
            self.assertEqual(list(one), [e[0] for e in made["reviewer"]])
            self.assertEqual(list(after), [e[0] for e in made["implementer"][1:]])
            for scan in (whole, one, after):
                scan.close()
            implementer = [e[0] for e in made["implementer"]] + [gone[0]]
            learner = [e[0] for e in lane]
            reviewer = [e[0] for e in made["reviewer"]] + [more[0]]
            self.assertEqual(scanned(store), implementer + learner + reviewer)
            self.assertEqual(scanned(again), implementer + learner + reviewer)
            self.assertEqual(scanned(again, scope=SCOPE, lane="reviewer"), reviewer)

    def create(self, drive, name, refuse=()):
        """A create of name/store.db under the Site's root, the countdowns of
        refuse armed: (error or None, the refused countdowns)."""
        reply = drive.store_open_at(f"{name}/store.db", create=True, refuse=refuse)
        return error_of(reply), reply.get("refused", [])

    def sweep(self, drive, kind, first=1, last=None):
        """S28's pass of one kind: each of its calls refused in turn, k from
        first until a create completes with its countdown not fired, or to
        last: a typed error, nothing left, the next create made. The
        refused runs."""
        root, refused = self.site_root(), 0
        for k in range(first, 5000 if last is None else last + 1):
            error, counts = self.create(drive, "c", [(kind, k)])
            if error is None and not counts[0]["fired"]:
                shutil.rmtree(root / "c")
                break
            if error is not None:
                refused += 1
                self.assertIn(error["kind"], TYPED, f"{kind} {k}: {error}")
                self.assertTrue(counts[0]["fired"], f"{kind} {k}: {error}")
                self.assertNotIn("removal refused", error["detail"], f"{kind} {k}")
                self.assertFalse((root / "c").exists(), f"{kind} {k}: {error}")
                self.assertEqual(self.create(drive, "c")[0], None, f"{kind} {k}: the next create")
            shutil.rmtree(root / "c")
        else:
            if last is None:
                self.fail(f"{kind}: no create completed in 5,000 calls")
        if first == 1:
            self.assertGreater(refused, 0, kind)
        return refused

    def faults_drive(self):
        """The drive of an S28 case, its tags and its build checked."""
        drive = self.drive()
        try:
            self.needs(drive, "own: sqlite")
            if not has_faults():
                self.skipTest("not applicable: built without QIVEN3_FAULTS, so no refusal")
        except unittest.SkipTest:
            drive.close()
            raise
        return drive

    def s28_part(self, part):
        """S28's part (from 0) of the refusals: create with each of its
        platform calls, its arenas' allocations, its engine calls and its
        engine heap's allocations refused in turn, the points of the part
        (S28_PARTS): a typed error, no file left nor the directory it made,
        and the next create made. The parts rise from pass 0's k 1, and the
        class holds every part's case, and none past them."""
        self.assertTrue(S28_PARTS[0] == (0, 1) and all(a < b for a, b in zip(S28_PARTS, S28_PARTS[1:])), S28_PARTS)
        held = [hasattr(type(self), f"test_S28_refused_part{i}") for i in range(1, len(S28_PARTS) + 2)]
        self.assertEqual(held, [True] * len(S28_PARTS) + [False])
        with self.faults_drive() as drive:
            for kind, name in enumerate(KINDS):
                if (ks := s28_range(part, kind)) is not None:
                    with self.subTest(kind=name, first=ks[0], last=ks[1]):
                        self.sweep(drive, name, *ks)

    def test_S28_unremoved(self):
        """S28: a refused create's removals refused too: with a file's, the
        step's error naming it, the file kept and the next create
        StoreError exists; with the directory's alone, the step's error
        naming it, the directory kept empty and the next create made; a
        directory there before kept. Needs: own: sqlite."""
        with self.faults_drive() as drive:
            root = self.site_root()
            for removal, name in enumerate(REMOVALS, start=2):
                with self.subTest(removal=name):
                    error, counts = self.create(drive, "r", [("engine_call", removal), ("engine_heap", 1)])
                    self.assertTrue(all(c["fired"] for c in counts), counts)
                    self.assertEqual(error["kind"], "memory", error)
                    self.assertTrue(error["detail"].endswith("removal refused: " + name), error)
                    self.assertEqual([p.name for p in (root / "r").iterdir()], ["store.db"])
                    self.assertEqual(self.create(drive, "r")[0]["code"], "exists")
                    shutil.rmtree(root / "r")
            error, counts = self.create(drive, "d", [("platform", 6), ("engine_heap", 1)])
            self.assertTrue(all(c["fired"] for c in counts), counts)
            self.assertEqual(error["kind"], "memory", error)
            self.assertTrue(error["detail"].endswith("removal refused: the directory"), error)
            self.assertEqual(list((root / "d").iterdir()), [])
            self.assertEqual(self.create(drive, "d"), (None, []))
            (root / "before").mkdir()
            error, counts = self.create(drive, "before", [("engine_heap", 1)])
            self.assertEqual(error["kind"], "memory", error)
            self.assertNotIn("removal refused", error["detail"])
            self.assertEqual(list((root / "before").iterdir()), [])
            self.assertEqual(self.create(drive, "before"), (None, []))


def s28_case(part):
    """S28's part (from 0) as a case of StoreRest's, named and described."""
    def case(self):
        self.s28_part(part)
    kind, k = S28_PARTS[part]
    case.__name__ = f"test_S28_refused_part{part + 1}"
    case.__doc__ = (f"S28, part {part + 1} of {len(S28_PARTS)}, from {KINDS[kind]}'s k {k}: as "
                    "StoreRest.s28_part. Needs: own: sqlite.")
    return case


for _part in range(len(S28_PARTS)):
    setattr(StoreRest, f"test_S28_refused_part{_part + 1}", s28_case(_part))
