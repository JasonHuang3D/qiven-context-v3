"""The ruled store's rules as the contract's cases see them, each defined
once over a store, through the fake's drive started with --store: mixed
into the bindings (contract/test_store_sqlite.py,
contract/test_store_memory.py) beside store_cases.py's, store_more.py's
and store_rest.py's, with their frame (StoreBinding: drive(), needs(), the
typed error's check; StoreCases' nothing_written and refuse).

Here: the entry format's parser and validator tables (S4, part 1), the
secret check's clearing (S4's four fixtures, S26) and the whole record a
forget takes (S25), each a rule of checked's, not the store's. The store's
own contract stays in store_cases.py, store_more.py and store_rest.py, with
the cases that test a store rule beside one of checked's (S3, S4 part 2,
S9, S13's refused sources, S16).
"""

from tests.contract.store_entries import (CLEARING, HEADER_ROWS, PARSER_ROWS, PERSON, SCOPE, Maker, at, body_rows,
                                          changed_header, clearing, guard_of, header, id_of, large, oversized,
                                          snapshot)


class CheckedCases:
    def test_S4_refusals(self):
        """S4, part 1: the parser's and the validator's tables of
        the entry format (each row's bytes a header line; A changed once, its
        id kept), each EntryInvalid with its code; a content past 65,536
        bytes and its field's bound, too_long at the field; a content past
        65,536 bytes with every field within its limit (M2's, its U+0001
        escaped), too_large, and at the bound: 65,537 bytes too_large,
        65,536 admitted; nothing written by a refusal. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make()
            store.append(PERSON, "implementer", None, m.chain(PERSON, "implementer", 1))
            a, b = m.held("A", "B")
            for line, code in PARSER_ROWS:
                with self.subTest(parser=code, line=line[:20]):
                    self.refuse(store, SCOPE, "implementer", None, [(line, a[1])], "EntryInvalid", code, index=0)
            for code, changes in HEADER_ROWS:
                with self.subTest(header=code, changes=changes):
                    lane = changes.get("lane", "implementer")
                    self.refuse(store, SCOPE, lane, None, [(changed_header(a, changes), a[1])], "EntryInvalid", code)
            for code, record in body_rows(a, b):
                with self.subTest(body=code):
                    self.refuse(store, SCOPE, "implementer", None, [(a[0], record)], "EntryInvalid", code)
            self.refuse(store, SCOPE, "implementer", None, [oversized(m)], "EntryInvalid", "too_long",
                        field="admit.mistake")
            for size in (None, 65537):
                with self.subTest(too_large=size):
                    self.refuse(store, SCOPE, "reviewer", None, [large(m, size)], "EntryInvalid", "too_large",
                                index=0)
            store.append(SCOPE, "reviewer", None, [large(m, 65536)])
            store.append(SCOPE, "implementer", None, [a])

    def test_S4_clearing(self):
        """S4, the four fixtures of a false secret hit's clearing: each appended, refused
        secret; cleared by a direction, admitted; the same body under the
        same key again, secret; again after an export imported into
        an empty store, and after a forget of the admitted entry, secret;
        cleared for another pattern or another digest, secret. Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m = Maker(drive)
            for value, pattern in CLEARING:
                with self.subTest(pattern=pattern, value=value[:8]):
                    store = drive.store_make()
                    hit = m.claim(SCOPE, "implementer", 1, None, 240, mistake=value)
                    self.refuse(store, SCOPE, "implementer", None, [hit], "EntryInvalid", "secret", pattern=pattern)
                    digest = header(hit)["content"]
                    store.append(SCOPE, "reviewer", None, [clearing(m, digest, pattern, 1, None, 60)])
                    store.append(SCOPE, "implementer", None, [hit])
                    again = m.claim(SCOPE, "implementer", 2, hit, 241, mistake=value)
                    self.assertEqual(header(again)["content"], digest)
                    self.refuse(store, SCOPE, "implementer", id_of(hit), [again], "EntryInvalid", "secret")
                    into = drive.store_make()
                    export = store.export()
                    into.import_(export)
                    export.drop()
                    self.refuse(into, SCOPE, "implementer", id_of(hit), [again], "EntryInvalid", "secret")
                    gone = m.forget(SCOPE, "implementer", 2, hit, hit, 242)
                    store.forget(SCOPE, "implementer", id_of(hit), [gone], guard_of(store))
                    after = m.claim(SCOPE, "implementer", 3, gone, 243, mistake=value)
                    self.refuse(store, SCOPE, "implementer", id_of(gone), [after], "EntryInvalid", "secret")
                    other = drive.store_make()
                    wrong = clearing(m, digest, "sk-key" if pattern != "sk-key" else "aws-akia", 1, None, 60)
                    elsewhere = clearing(m, "hmac-sha256:" + "0" * 64, pattern, 2, wrong, 61)
                    other.append(SCOPE, "reviewer", None, [wrong, elsewhere])
                    self.refuse(other, SCOPE, "implementer", None, [hit], "EntryInvalid", "secret", pattern=pattern)

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
        """S25: (a) v1 and its revision v2 in one lane: forget v2,
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
        """S26, on the first clearing fixture: a direction of
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
