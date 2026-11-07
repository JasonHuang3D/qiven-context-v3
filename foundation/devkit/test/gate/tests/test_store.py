"""store: on a planted store in a scratch directory, state.json replaced whole
or not at all, the lock held against a second process that names its holder,
the tries on a held file, an unreadable store refused, the ids' forms and the
retention. A store wrong in silence loses a kept red, or lets two runs write
one configuration's states."""

import json
import sys
import tempfile
import threading
import unittest
from pathlib import Path

from foundation.devkit.python import process
from foundation.devkit.python import system
from foundation.devkit.python.build_tree import ROOT
from foundation.devkit.test.gate import store

# A second process asks for the lock of the store argv[1] names and prints what it got.
SECOND = (f"import json, sys; sys.path.append({str(ROOT)!r}); "
          "from foundation.devkit.test.gate import store\n"
          "try:\n"
          "    store.lock(sys.argv[1], ['second']).release(); print('taken')\n"
          "except store.Locked as e:\n"
          "    print(json.dumps(e.holder))\n")
SECOND_SECONDS = 60


class Store(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="qiven-gate-store-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.place = store.place(self.root, "ci-windows")

    def test_replaced_whole(self):
        """write then read gives the state, format 1, no temporary left; a
        leftover state.json.tmp of a killed run is no state and is written over."""
        self.assertIsNone(store.read(self.place))
        self.place.mkdir(parents=True)
        (self.place / "state.json.tmp").write_bytes(b'{"format": 1, "tests": {"half')
        state = store.empty("ci-windows")
        state["tests"]["a.b"] = {"key": "k", "outcome": "failed"}
        store.write(self.place, state)
        self.assertEqual(store.read(self.place), {**state, "format": store.FORMAT})
        self.assertFalse((self.place / "state.json.tmp").exists())

    def test_unreadable(self):
        """A state.json that does not parse, is no object or lacks a field is
        Unreadable, never no store."""
        self.place.mkdir(parents=True)
        whole = json.dumps(store.empty("ci-windows"))
        for planted in (b"{not json", b"[]", json.dumps({"format": 1}).encode(), b"\xff\xfe",
                        whole.replace('"tests": {}', '"tests": []').encode()):
            with self.subTest(planted=planted[:20]):
                (self.place / "state.json").write_bytes(planted)
                with self.assertRaises(store.Unreadable):
                    store.read(self.place)

    @unittest.skipUnless(system.WINDOWS, "a file held open blocks os.replace on Windows alone")
    def test_tries(self):
        """A state.json held open by a reader: the write tries over 2 s, then
        WriteFailed, the old state whole; a reader that lets go within the
        tries lets the write through."""
        self.place.mkdir(parents=True)
        store.write(self.place, store.empty("ci-windows"))
        newer = {**store.empty("ci-windows"), "run": "r2"}
        with open(self.place / "state.json", "rb"):
            with self.assertRaises(store.WriteFailed):
                store.write(self.place, newer)
        self.assertIsNone(store.read(self.place)["run"])
        held = open(self.place / "state.json", "rb")
        timer = threading.Timer(store.TRY_SECONDS * 1.5, held.close)
        timer.start()
        try:
            store.write(self.place, newer)
        finally:
            timer.join()
            held.close()
        self.assertEqual(store.read(self.place)["run"], "r2")

    @unittest.skipUnless(system.WINDOWS, "system.lock_byte has no back end off Windows")
    def test_lock(self):
        """Held, the lock refuses a second process, which reads the holder;
        released, the second takes it."""
        argv = [sys.executable, "-I", "-c", SECOND, str(self.place)]
        with store.lock(self.place, ["gate", "commit"]):
            second = process.run(argv, limit=SECOND_SECONDS, text=True)
            self.assertEqual(second.status, 0, second.err)
            holder = json.loads(second.out)
            self.assertEqual(holder["argv"], ["gate", "commit"])
            self.assertIn("pid", holder)
        second = process.run(argv, limit=SECOND_SECONDS, text=True)
        self.assertEqual((second.status, second.out.strip()), (0, "taken"), second.err)

    def test_others(self):
        """Another configuration's state.json is read; this one's is not."""
        self.place.mkdir(parents=True)
        store.write(self.place, store.empty("ci-windows"))
        other = store.place(self.root, "ci-asan")
        other.mkdir(parents=True)
        store.write(other, {**store.empty("ci-asan"), "build": {"since": "r1"}})
        (self.root / ".local" / "states" / "ci-empty").mkdir()
        self.assertEqual(list(store.others(self.root, "ci-windows")), ["ci-asan"])
        self.assertEqual(store.others(self.root, "ci-windows")["ci-asan"]["build"], {"since": "r1"})

    def test_ids(self):
        """A run's id is <UTC time>-<4 hex>; a tree's, 16 hex digits written
        once and read again; one that is no id is Unreadable."""
        self.assertEqual(store.run_id(0)[:17], "19700101T000000Z-")
        self.assertRegex(store.run_id(), r"^\d{8}T\d{6}Z-[0-9a-f]{4}$")
        self.assertEqual(store.spelled("x"), "run:x")
        made = store.tree_id(self.root)
        self.assertRegex(made, r"^[0-9a-f]{16}$")
        self.assertEqual(store.tree_id(self.root), made)
        (self.root / "states.id").write_text("nonsense\n", encoding="ascii")
        with self.assertRaises(store.Unreadable):
            store.tree_id(self.root)

    def test_retention(self):
        """The newest KEEP runs' files and each cited run's are kept, the rest
        removed; a journal with no end line is unended."""
        runs = [f"20261107T{n:06d}Z-00aa" for n in range(store.KEEP + 5)]
        for run in runs:
            with store.Journal(self.place, run) as journal:
                journal.write("header", gate="commit")
                if run != runs[-1]:
                    journal.write("end", code="PASSED")
            store.run_file(self.place, run, ".log").write_text("log\n", encoding="utf-8")
        (self.place / "runs" / "notes.txt").write_text("not a run's\n", encoding="utf-8")
        self.assertEqual(store.unended(self.place), [runs[-1]])
        self.assertEqual(store.retain(self.place, [store.spelled(runs[1])]), [])
        left = sorted(p.name for p in (self.place / "runs").iterdir())
        kept = sorted({runs[1], *runs[-store.KEEP:]})
        self.assertEqual(left, sorted([f"{r}{s}" for r in kept for s in (".jsonl", ".log")] + ["notes.txt"]))
