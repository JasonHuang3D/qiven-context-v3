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

Here: S1 to S9, S4's own SQLite part its plans, and beside S8 the open's
write probe on a file no one may write; S4's part 1 and its clearing,
checked's rules, are checked_cases.py's. The entries are made through the
drive (store_entries.py).
"""

import json
import os
import shutil
import stat
import tempfile
import unittest
from pathlib import Path

from tests.contract.store_entries import (CLEARING, LANES, PERSON, SCOPE, Maker, chain_fields, clearing, guard_of,
                                          header, id_of, same_files, same_snapshot, scanned, snapshot, snapshot_files)
from tests.drive.session import FAKE, Drive, DriveError

# The tags a case may name, in the order a reason lists them.
TAGS = ("durable", "files", "processes", "own: sqlite")
# The statements the SQLite store plans: the view's reads, then the
# export's.
PLANNED = ("keyed_roots", "record_members", "target_members", "held_line", "held_scope", "content_scan",
           "forget_counts", "head_rows", "lane_rows")


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

    def nothing_written(self, store, before, what):
        """A refusal left the store as the snapshot before it."""
        differs = same_snapshot(before, snapshot(store))
        self.assertIsNone(differs, f"{what} wrote: {differs}")

    def test_S2_append_and_read(self):
        """S2: 3 entries appended to each of 3 lanes in 2 scopes: heads at
        seq 3; headers and bodies read back byte-equal; scan in (scope,
        lane, seq) order. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            made = {}
            for scope in (SCOPE, PERSON):
                for lane in LANES:
                    entries = m.chain(scope, lane, 3)
                    written = store.append(scope, lane, None, entries)
                    self.assertEqual(written["head"], {"id": id_of(entries[-1]), "seq": 3})
                    made[scope, lane] = entries
            for (scope, lane), entries in made.items():
                self.assertEqual(store.head(scope, lane), {"id": id_of(entries[-1]), "seq": 3})
                for entry in entries:
                    self.assertEqual(store.header(id_of(entry)), entry[0])
                    self.assertEqual(store.body(id_of(entry)), entry[1])
            order = sorted(made, key=lambda k: (k[0].encode(), k[1].encode()))
            rows = [{"scope": s, "lane": l, "id": id_of(made[s, l][-1]), "seq": 3} for s, l in order]
            self.assertEqual(store.heads()["rows"], rows)
            self.assertEqual(scanned(store), [e[0] for key in order for e in made[key]])
            self.assertEqual(store.size()["volume"]["count"], 18)

    def test_S3_all_or_none(self):
        """S3: an append whose third entry is invalid, by part 1 (a check
        with no target) and by part 2 (a seq past the parent's next):
        EntryInvalid at index 2; nothing written. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            first = m.chain(SCOPE, "implementer", 1)
            store.append(SCOPE, "implementer", None, first)
            good = m.chain(SCOPE, "implementer", 2, after=first[0], minute=1)
            third = {"target": m.on("A", SCOPE, "implementer", 4, good[1], 3, kind="check"),
                     "seq": m.claim(SCOPE, "implementer", 5, good[1], 3)}
            for code, bad in third.items():
                with self.subTest(code=code):
                    before = snapshot(store)
                    error = self.failure(store.append, SCOPE, "implementer", id_of(first[0]), [*good, bad])
                    # A header's fault comes before the header is read
                    # through: no id (part 1); part 2's names the entry.
                    self.assert_store_error(error, "EntryInvalid", code, index=2,
                                            id="" if code == "target" else id_of(bad))
                    self.nothing_written(store, before, code)
            self.assertEqual(store.head(SCOPE, "implementer")["seq"], 1)

    def test_S5_conflict(self):
        """S5: two store objects read head h; one appends; the other's
        append expecting h, and one expecting none, are HeadConflict; a
        forget whose guard another lane of its scope, a lane of another
        scope or a new lane has moved past is HeadConflict
        naming that lane; nothing written. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            other = drive.store_again(store.state)
            a = m.chain(SCOPE, "implementer", 1)
            store.append(SCOPE, "implementer", None, a)
            h = other.head(SCOPE, "implementer")["id"]
            self.assertEqual(h, store.head(SCOPE, "implementer")["id"])
            one = m.claim(SCOPE, "implementer", 2, a[0], 1, text="The first writer's.")
            two = m.claim(SCOPE, "implementer", 2, a[0], 2, text="The second writer's.")
            store.append(SCOPE, "implementer", h, [one])
            refused = (("stale", h, [two], h), ("none", None, m.chain(SCOPE, "implementer", 1, minute=3), ""))
            for what, expected, entries, named in refused:
                with self.subTest(append=what):
                    before = snapshot(store)
                    error = self.failure(other.append, SCOPE, "implementer", expected, entries)
                    self.assert_store_error(error, "HeadConflict", scope=SCOPE, lane="implementer",
                                            expected=named, actual=id_of(one))
                    self.nothing_written(store, before, what)
            heads = {(SCOPE, "reviewer"): m.chain(SCOPE, "reviewer", 1)[0],
                     (PERSON, "implementer"): m.chain(PERSON, "implementer", 1)[0]}
            for (scope, lane), entry in heads.items():
                store.append(scope, lane, None, [entry])
            moves = (("its scope's", SCOPE, "reviewer"), ("another scope's", PERSON, "implementer"),
                     ("a new", SCOPE, "learner"))
            for minute, (what, scope, lane) in enumerate(moves, 10):
                with self.subTest(guard=what):
                    guard = [(r["scope"], r["lane"], r["id"]) for r in store.heads()["rows"]]
                    old = heads.get((scope, lane))
                    moved = m.chain(scope, lane, 1, after=old, minute=minute)
                    other.append(scope, lane, None if old is None else id_of(old), moved)
                    heads[scope, lane] = moved[0]
                    forget = m.forget(SCOPE, "implementer", 3, one, one, 100 + minute)
                    before = snapshot(store)
                    error = self.failure(store.forget, SCOPE, "implementer", id_of(one), [forget], guard)
                    self.assert_store_error(error, "HeadConflict", scope=scope, lane=lane,
                                            expected="" if old is None else id_of(old), actual=id_of(moved[0]))
                    self.nothing_written(store, before, what)
            guard = [(r["scope"], r["lane"], r["id"]) for r in store.heads()["rows"]]
            written = store.forget(SCOPE, "implementer", id_of(one), [m.forget(SCOPE, "implementer", 3, one, one, 120)],
                                   guard)
            self.assertEqual(written["erased"], [id_of(one)])

    def test_S6_independent_heads(self):
        """S6: lanes (person, implementer), (person, reviewer),
        (project:qiven, implementer); a conflict on one: the others move,
        the conflict moves none. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            lanes = ((PERSON, "implementer"), (PERSON, "reviewer"), (SCOPE, "implementer"))
            roots = {key: m.chain(*key, 1)[0] for key in lanes}
            for key, entry in roots.items():
                store.append(*key, None, [entry])
            conflicted = (PERSON, "reviewer")
            error = self.failure(store.append, *conflicted, None, m.chain(*conflicted, 1, minute=5))
            self.assert_store_error(error, "HeadConflict", scope=PERSON, lane="reviewer", expected="",
                                    actual=id_of(roots[conflicted]))
            for key in lanes:
                if key == conflicted:
                    continue
                nxt = m.chain(*key, 1, after=roots[key], minute=6)
                self.assertEqual(store.append(*key, id_of(roots[key]), nxt)["head"], {"id": id_of(nxt[0]), "seq": 2})
            for key in lanes:
                seq = 1 if key == conflicted else 2
                self.assertEqual(store.head(*key)["seq"], seq, key)
            self.assertEqual(store.head(*conflicted)["id"], id_of(roots[conflicted]))

    def test_S7_reads_see_commits(self):
        """S7: an append through one store object, read through again and
        through read_only, each made before it: seen at once. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            readers = {"again": drive.store_again(store.state), "read_only": drive.store_read_only(store.state)}
            for reader in readers.values():
                self.assertIsNone(reader.head(SCOPE, "implementer"))
            entries = m.chain(SCOPE, "implementer", 2)
            store.append(SCOPE, "implementer", None, entries)
            for name, reader in readers.items():
                with self.subTest(reader=name):
                    self.assertEqual(reader.head(SCOPE, "implementer"), {"id": id_of(entries[-1]), "seq": 2})
                    self.assertEqual(reader.heads(), store.heads())
                    for entry in entries:
                        self.assertEqual(reader.header(id_of(entry)), entry[0])
                        self.assertEqual(reader.body(id_of(entry)), entry[1])
                    self.assertEqual(scanned(reader), [e[0] for e in entries])

    def test_S8_durable_on_return(self):
        """S8: append, close, reopen: present, heads equal. Needs: durable."""
        with self.drive() as drive:
            self.needs(drive, "durable")
            m, store = Maker(drive), drive.store_make()
            entries = m.chain(SCOPE, "implementer", 3)
            store.append(SCOPE, "implementer", None, entries)
            heads, lines = store.heads(), scanned(store)
            store.close()
            back = drive.store_reopen(store.state)
            self.assertEqual(back.heads(), heads)
            self.assertEqual(scanned(back), lines)
            for entry in entries:
                self.assertEqual(back.body(id_of(entry)), entry[1])
            self.assertEqual(back.verify()["entries"], 3)

    def test_S8_read_only_file(self):
        """Beside S8: a store's file the system lets no one write, opened
        read-write: the open's write probe refuses StoreLocked not_writable
        at once, the database and -wal byte-equal (a -wal absent before
        absent or empty after). Needs: durable, files."""
        with self.drive() as drive:
            self.needs(drive, "durable", "files")
            m, store = Maker(drive), drive.store_make()
            store.append(SCOPE, "implementer", None, m.chain(SCOPE, "implementer", 1))
            store.close()
            path = Path(store.path)
            mode = path.stat().st_mode
            path.chmod(stat.S_IREAD)
            try:
                self.assertFalse(os.access(path, os.W_OK), "the file stays writable: a root user's run")
                before = snapshot_files(store)
                error = self.failure(drive.store_reopen, store.state)
                self.assert_store_error(error, "StoreLocked", "not_writable")
                self.assertIsNone(same_files(before, snapshot_files(store)))
            finally:
                # The drive's end removes the file: writable again first.
                path.chmod(mode)

    def test_S9_forget(self):
        """S9: B forgotten by E: B's body absent, forgotten; its header and
        id kept; D's parent resolves; the head moved; the size less B's
        record plus E's line, the count plus 1; the purge done. A second
        forget of B: EntryInvalid target; a claim and a check given to
        forget: StoreError forget_kind; a forget of a version whose
        revision is neither forgotten nor in the set: whole_record;
        each writing nothing, the store then opening coherent. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            a, b, c, d, acc, e, l1, l2 = m.held("A", "B", "C", "D", "ACC", "E", "L1", "L2")
            store.append(SCOPE, "implementer", None, [a])
            store.append(SCOPE, "reviewer", None, [c])
            store.append(SCOPE, "implementer", id_of(a), [b, d, acc])
            store.append(SCOPE, "learner", None, [l1, l2])
            before = store.size()["volume"]
            forgets = store.heads()["forgets"]
            written = store.forget(SCOPE, "implementer", id_of(acc), [e], guard_of(store))
            self.assertEqual(written["erased"], [id_of(b)])
            self.assertEqual(written["head"], {"id": id_of(e), "seq": 5})
            self.assertTrue(written["purge"]["done"], written)
            self.assertIsNone(store.body(id_of(b)))
            self.assertTrue(store.forgotten(id_of(b)))
            self.assertFalse(store.forgotten(id_of(a)))
            self.assertEqual(store.header(id_of(b)), b[0])
            self.assertEqual(store.header(header(d)["parent"]), b[0])
            self.assertEqual(store.head(SCOPE, "implementer"), {"id": id_of(e), "seq": 5})
            volume = store.size()["volume"]
            self.assertEqual(volume["size"], before["size"] - (len(b[1]) + 1) + (len(e[0]) + 1))
            self.assertEqual(volume["count"], before["count"] + 1)
            heads = store.heads()
            self.assertEqual((heads["forgets"], heads["purge"]["done"]), (forgets + 1, True))
            refused = (
                ("target", "EntryInvalid", "implementer", e, [m.forget(SCOPE, "implementer", 6, e, b, 200)]),
                ("claim", "StoreError", "implementer", e, [m.claim(SCOPE, "implementer", 6, e, 200)]),
                ("check", "StoreError", "reviewer", c, [m.on("C", SCOPE, "reviewer", 2, c, 200, target=id_of(a))]),
                ("whole_record", "StoreError", "learner", l2, [m.forget(SCOPE, "learner", 3, l2, l1, 200)]),
            )
            for what, cls, lane, head, entries in refused:
                with self.subTest(forget=what):
                    snap = snapshot(store)
                    error = self.failure(store.forget, SCOPE, lane, id_of(head), entries, guard_of(store))
                    self.assert_store_error(error, cls, "forget_kind" if what in ("claim", "check") else what)
                    self.nothing_written(store, snap, what)
            opened = drive.store_again(store.state)
            self.assertEqual(opened.verify(), store.verify())
            self.assertEqual(opened.verify()["entries"], 8)

    def refuse(self, store, scope, lane, expected, entries, cls, code, **fields):
        """The append refused as stated, nothing written; its error."""
        before = snapshot(store)
        error = self.failure(store.append, scope, lane, expected, entries)
        self.assert_store_error(error, cls, code, **fields)
        self.nothing_written(store, before, code)
        return error

    def test_S4_part_two(self):
        """S4, part 2 and the append's own: one entry a code (a parent not
        the head, a seq gap, a target missing or of another scope, an at
        out of order, an entry reference not held, a revision of a
        forgotten version, a revision changing its record's identity, a
        decision citing a map); a forget, with content and without;
        a secret in the header and in the content, named by its
        pattern and path, never by its value or the id. Each EntryInvalid,
        nothing written. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            a, b, c, m1 = m.held("A", "B", "C", "M1")
            store.append(SCOPE, "implementer", None, [a, b])
            store.append(SCOPE, "reviewer", None, [c, m1])
            p1 = m.chain(PERSON, "implementer", 1)[0]
            store.append(PERSON, "implementer", None, [p1])
            gone = m.forget(PERSON, "implementer", 2, p1, p1, 10)
            store.forget(PERSON, "implementer", id_of(p1), [gone], guard_of(store))
            claim = m.content("A")
            cited = m.content("C")
            cited["evidence"].append({"id": "sha256:" + "1" * 64, "type": "entry"})
            moved = m.content("M2")
            moved["object"]["path"] = "qiven3/other.py"
            decision = {k: v for k, v in claim.items() if k != "claim"}
            decision.update(decides="Keep the strict parser.", evidence=[{"id": id_of(m1), "type": "entry"}])
            forget_b = m.forget(SCOPE, "implementer", 3, b, b, 200)
            rows = (
                ("parent", "implementer", b, m.claim(SCOPE, "implementer", 2, a, 200)),
                ("seq", "implementer", b, m.claim(SCOPE, "implementer", 4, b, 200)),
                ("target", "reviewer", m1, m.on("C", SCOPE, "reviewer", 3, m1, 200, target="sha256:" + "0" * 64)),
                ("target", "reviewer", m1, m.on("C", SCOPE, "reviewer", 3, m1, 200, target=id_of(p1))),
                ("at_order", "implementer", b, m.claim(SCOPE, "implementer", 3, b, 0)),
                ("reference", "reviewer", m1, m.on("C", SCOPE, "reviewer", 3, m1, 200, cited, target=id_of(a))),
                ("identity", "reviewer", m1, m.made("M2", moved)),
                ("map_evidence", "implementer", b, m.on("A", SCOPE, "implementer", 3, b, 200, decision,
                                                        kind="decision")),
                ("forget_path", "implementer", b, forget_b),
                ("content", "implementer", b, m.made("E", m.content("A"), key_of="A", **chain_fields(forget_b))),
            )
            for code, lane, head, entry in rows:
                with self.subTest(code=code, entry=header(entry)):
                    self.refuse(store, SCOPE, lane, id_of(head), [entry], "EntryInvalid", code, index=0)
            revision = m.on("A", PERSON, "implementer", 3, gone, 20, claim, target=id_of(p1))
            self.refuse(store, PERSON, "implementer", id_of(gone), [revision], "EntryInvalid", "forgotten_target")
            lane = "xoxb" + "-1-" + "abcdefgh"
            in_header = m.chain(SCOPE, lane, 1)[0]
            value = "a key " + "AKIA" + "IOSFODNN7EXAMPLE"
            in_content = m.claim(SCOPE, "implementer", 3, b, 200, mistake=value)
            for where, scope_lane, entry, pattern, secret in (
                    ("header", lane, in_header, "slack-xox", lane), ("content", "implementer", in_content,
                                                                     "aws-akia", "AKIA")):
                with self.subTest(secret=where):
                    expected = None if scope_lane == lane else id_of(b)
                    error = self.refuse(store, SCOPE, scope_lane, expected, [entry], "EntryInvalid", "secret",
                                        pattern=pattern)
                    self.assertTrue(error.error["field"], error.error)
                    text = json.dumps(error.error)
                    self.assertNotIn(secret, text)
                    self.assertNotIn(id_of(entry), text)

    def test_S4_plans(self):
        """S4's own SQLite part: the view's two reads, the clearings (a
        scope's roots of a kind, a record's members, a line by id, each
        member's row) and the scan of entry.line for bytes, and beside them
        the export's
        statements, planned with no structure built in memory as large as
        its rows: no plan line holds TEMP B-TREE, AUTOMATIC, MATERIALIZE or
        BLOOM FILTER, on a store whose append ran both reads. The store runs
        no ANALYZE, so its plans do not follow its row count; the times at
        555,000 entries are the slow group's. Needs: own: sqlite."""
        with self.drive() as drive:
            self.needs(drive, "own: sqlite")
            m, store = Maker(drive), drive.store_make()
            value, pattern = CLEARING[0]
            hit = m.claim(SCOPE, "implementer", 1, None, 240, mistake=value)
            store.append(SCOPE, "reviewer", None, [clearing(m, header(hit)["content"], pattern, 1, None, 60)])
            store.append(SCOPE, "implementer", None, [hit])
            planned = {item["name"]: item["lines"] for item in store.plans()}
            self.assertEqual(sorted(planned), sorted(PLANNED))
            for name, lines in planned.items():
                with self.subTest(statement=name):
                    self.assertTrue(lines, name)
                    for line in lines:
                        for bad in ("TEMP B-TREE", "AUTOMATIC", "MATERIALIZE", "BLOOM FILTER"):
                            self.assertNotIn(bad, line, f"{name}: {lines}")
