"""The platform's cases P3 to P8 and P16, each defined once over an adapter
(contract/platform_cases.py names the binding's part): location's commands,
init, change and show, run by its main on the adapter as the binary runs
them, through the drive's location request; store_dir, the live store's
directory as the commands that open it read it.

Each drive's configuration directory is "<files root>/config", its clock
set to CLOCK_US, its facts each off on volume V: unless the case changes
them. On the fake the commands' stream is the fake's, as the request's
"out". The Windows drive has no location request yet (its commands write
to the drive's own stream there): until it has, a binding with LOCATION
false skips these cases.
"""

import json
import os
import shutil
import sys
from pathlib import Path

CLOCK_US = 1_792_229_400_123_000
AT = "2026-10-17T09:30:00.123Z"
LATER_US = CLOCK_US + 3_600_000_000
LATER_AT = "2026-10-17T10:30:00.123Z"
# A command's budget: its arena's peak stays within it.
BUDGET = 1 << 20

OFF = {"volume": "V:", "protection": "off", "file_history": "off", "bitlocker": "off"}

# The drive check's conditions: the name it prints, what a warning adds,
# what an off BitLocker adds, and how to look.
CONDITIONS = {
    "protection": ("System Protection",
                   "Restore points keep earlier blocks of the drive's files, which v3 cannot forget. ", "",
                   "To look: System Properties, its System Protection tab (SystemPropertiesProtection, "
                   "which asks for an administrator).\n"),
    "file_history": ("File History",
                     "Its versions keep copies of the folders it includes, which v3 cannot forget. ", "",
                     "To look: the Control Panel's File History.\n"),
    "bitlocker": ("BitLocker",
                  "It keeps no copy and encrypts at rest, and the store cannot be opened while the drive is "
                  "locked. ", "Nothing encrypts the store at rest. ",
                  "To look: the Control Panel's BitLocker Drive Encryption.\n"),
}

NOT_ACCEPTED = "not accepted: nothing recorded; run again with --accept to go on\n"
MOVED = ("the old drive keeps what a move left: a move to another volume leaves the deleted files' clusters, "
         "and its restore points and File History versions keep their copies; v3 reaches none of them\n")

# The files of v3's a change will not leave behind.
LEFT = ("store.db", "map-marks.json")


def fact_lines(states=None):
    """The check's lines, each condition off but those states names."""
    states = states or {}
    lines = []
    for key, (name, why, off, look) in CONDITIONS.items():
        state = states.get(key, "off")
        if state == "off":
            lines.append(f"{name}: off (fake), volume V:. {off}{look}")
        else:
            lines.append(f"warning: {name}: {state} (fake), volume V:. {why}{look}")
    return "".join(lines)


def record_of(at, directory):
    """The record's canonical line."""
    return json.dumps({"at": at, "dir": str(directory)}, ensure_ascii=False, separators=(",", ":")) + "\n"


def recorded(at, directory, moved=False):
    """A recorded command's output: the check, the move's line where the
    directory changed, the record, then recorded."""
    return fact_lines() + (MOVED if moved else "") + record_of(at, directory) + f"recorded {directory}\n"


class PlatformLocation:
    LOCATION = True

    def located(self, config=None):
        """A drive for location's commands, its facts each off, its clock
        at CLOCK_US."""
        self.needs("files")
        if not self.LOCATION:
            self.skipTest("until package 5's adapter cases: the Windows drive has no location request")
        d = self.drive(config=str(config or Path(self.files_root()) / "config"))
        try:
            d.call("set_facts", **OFF)
            d.call("set_clock", us=CLOCK_US)
        except BaseException:
            d.close(check=False)
            raise
        return d

    def command(self, d, *argv):
        """The command's exit status, its output and the platform calls it
        made; its arena's peak within its budget."""
        result = d.call("location", argv=[str(a) for a in argv])
        self.assertLessEqual(result["peak"], BUDGET, result)
        return result["code"], bytes.fromhex(result["out"]).decode("utf-8"), result["calls"]

    def refusal(self, d):
        """store_dir's StoreError: its class, code, detail, operation, path."""
        refused = d.call("store_dir")["refused"]
        return refused["class"], refused["code"], refused["detail"], refused["operation"], refused["path"]

    def test_P3_record(self):
        """init into an empty directory; the record read back; records with
        a key added, dir relative, a control character, a duplicate key:
        each refused, StoreError location config."""
        root = Path(self.files_root())
        store, record = root / "store", root / "config" / "location.json"
        with self.located() as d:
            code, out, _ = self.command(d, "init", store)
            self.assertEqual((code, out), (0, recorded(AT, store)))
            self.assertEqual(record.read_bytes(), record_of(AT, store).encode())
            self.assertEqual(d.call("store_dir"), {"dir": str(store)})
            self.assertEqual(self.command(d, "show")[:2], (0, recorded(AT, store)))

            dir_text = json.dumps(str(store))[1:-1]
            forms = {
                "a key added": f'{{"at":"{AT}","dir":"{dir_text}","x":1}}\n',
                "dir relative": f'{{"at":"{AT}","dir":"store"}}\n',
                "a control character": f'{{"at":"{AT}","dir":"{dir_text}\\u0001x"}}\n',
                "a duplicate key": f'{{"at":"{AT}","dir":"{dir_text}","dir":"{dir_text}"}}\n',
            }
            other = root / "other"
            for name, form in forms.items():
                with self.subTest(record=name):
                    record.write_bytes(form.encode())
                    self.assertEqual(self.command(d, "show")[:2], (1, f"StoreError location: show {record}; config\n"))
                    self.assertEqual(self.command(d, "change", other, "--accept")[:2],
                                     (1, f"StoreError location: change {record}; config\n"))
                    self.assertEqual(self.refusal(d), ("StoreError", "location", "config", "store_dir", str(record)))
                    self.assertEqual(record.read_bytes(), form.encode())
                    self.assertFalse(other.exists())

    def test_P4_not_chosen(self):
        """The live store opened with no record: StoreError location
        not_chosen."""
        config = Path(self.files_root()) / "config"
        record = config / "location.json"
        with self.located() as d:
            self.assertEqual(self.refusal(d), ("StoreError", "location", "not_chosen", "store_dir", str(record)))
            self.assertFalse(config.exists())
            self.assertEqual(self.command(d, "show")[:2], (1, f"StoreError location: show {record}; not_chosen\n"))
            self.assertFalse(record.exists())

    def test_P5_guard(self):
        """init, then change, to a directory under .git (a directory, then a
        file), under a synced folder, and through a link the fake resolves
        into each: StoreError location checkout or synced, nothing recorded,
        no file made."""
        root = Path(self.files_root())
        record = root / "config" / "location.json"
        repo, worktree, cloud = root / "repo", root / "wt", root / "cloud"
        (repo / ".git").mkdir(parents=True)
        worktree.mkdir()
        (worktree / ".git").write_bytes(b"gitdir: elsewhere\n")
        cloud.mkdir()
        targets = (
            (repo / "s", repo / "s", "checkout"),
            (worktree / "s", worktree / "s", "checkout"),
            (cloud / "s", cloud / "s", "synced"),
            (root / "to-repo" / "s", repo / "s", "checkout"),
            (root / "to-cloud" / "s", cloud / "s", "synced"),
        )
        with self.located() as d:
            d.call("set_synced", folders=[[str(cloud), "fake drive"]])
            d.call("set_links", links=[[str(root / "to-repo"), str(repo)], [str(root / "to-cloud"), str(cloud)]])
            for op in ("init", "change"):
                if op == "change":
                    self.assertEqual(self.command(d, "init", root / "store")[0], 0)
                before = record.read_bytes() if record.exists() else None
                for given, resolved, detail in targets:
                    with self.subTest(op=op, target=str(given.relative_to(root))):
                        code, out, calls = self.command(d, op, given, "--accept")
                        self.assertEqual((code, out), (1, f"StoreError location: guard {resolved}; {detail}\n"))
                        self.assertNotIn("facts", calls)
                        self.assertNotIn("create", calls)
                        self.assertEqual(record.read_bytes() if record.exists() else None, before)
                        self.assertFalse(resolved.exists())

    def test_P6_warnings(self):
        """Each condition on, then unknown, and BitLocker locked, without
        and then with --accept: one line per condition, saying how to look;
        exit 4 and nothing recorded, then recorded."""
        root = Path(self.files_root())
        record = root / "config" / "location.json"
        cases = [(key, state) for key in CONDITIONS for state in ("on", "unknown")] + [("bitlocker", "locked")]
        with self.located() as d:
            for n, (key, state) in enumerate(cases):
                with self.subTest(condition=key, state=state):
                    d.call("set_facts", **{**OFF, key: state})
                    lines = fact_lines({key: state})
                    target = root / f"s{n}"
                    self.assertEqual(self.command(d, "init", target)[:2], (4, lines + NOT_ACCEPTED))
                    self.assertFalse(target.exists())
                    self.assertFalse(record.exists())
                    expected = lines + record_of(AT, target) + f"recorded {target}\n"
                    self.assertEqual(self.command(d, "init", target, "--accept")[:2], (0, expected))
                    self.assertTrue((target / "store.db").is_file())
                    self.assertEqual(record.read_bytes(), record_of(AT, target).encode())
                    record.unlink()

    def test_P7_init(self):
        """init with the configuration directory missing; with a record
        there; into a directory holding a store; into an empty one; with
        --config whose parent is missing, then naming a file."""
        root = Path(self.files_root())
        config = root / "config"
        record = config / "location.json"
        made, empty = root / "made", root / "empty"
        with self.located() as d:
            self.assertFalse(config.exists())
            code, out, calls = self.command(d, "init", made)
            self.assertEqual((code, out), (0, recorded(AT, made)))
            self.assertTrue(config.is_dir())
            self.assertIn("create", calls)
            self.assertTrue((made / "store.db").is_file())

            before = record.read_bytes()
            self.assertEqual(self.command(d, "init", root / "again")[:2],
                             (1, f"StoreError location: init {record}; chosen\n"))
            self.assertEqual(record.read_bytes(), before)
            self.assertFalse((root / "again").exists())

            record.unlink()
            code, out, calls = self.command(d, "init", made)
            self.assertEqual((code, out), (0, recorded(AT, made)))
            self.assertNotIn("create", calls)

            record.unlink()
            empty.mkdir()
            code, out, calls = self.command(d, "init", empty)
            self.assertEqual((code, out), (0, recorded(AT, empty)))
            self.assertIn("create", calls)
            self.assertTrue((empty / "store.db").is_file())

        target = root / "s"
        lost = root / "lost" / "config"
        a_file = root / "file"
        a_file.write_bytes(b"kept")
        for given, code_name in ((lost, "io"), (a_file, "exists")):
            with self.subTest(config=given.name), self.located(config=given) as d:
                code, out, calls = self.command(d, "init", target, "--config", given)
                self.assertEqual(code, 1)
                self.assertTrue(out.startswith(f"PlatformError {code_name}"), out)
                self.assertEqual(out.count("\n"), 1, out)
                self.assertNotIn("create", calls)
                self.assertFalse(target.exists())
        self.assertFalse(lost.parent.exists())
        self.assertEqual(a_file.read_bytes(), b"kept")

    def test_P8_change(self):
        """change to the same directory; the old one still holding store.db,
        then map-marks.json alone; an old one gone, then unreadable; a new
        one holding a store; neither holding one."""
        root = Path(self.files_root())
        record = root / "config" / "location.json"
        old, new, held = root / "old", root / "new", root / "held"
        with self.located(config=root / "elsewhere") as e:
            self.assertEqual(self.command(e, "init", held)[0], 0)
        with self.located() as d:
            self.assertEqual(self.command(d, "init", old)[0], 0)
            first = record.read_bytes()
            d.call("set_clock", us=LATER_US)
            self.assertEqual(self.command(d, "change", old)[:2], (0, recorded(AT, old)))
            self.assertEqual(record.read_bytes(), first)

            for name in LEFT:
                with self.subTest(left=name):
                    # store.db is init's store; map-marks.json alone after it.
                    if name != "store.db":
                        (old / name).write_bytes(b"x")
                    code, out, _ = self.command(d, "change", new)
                    self.assertEqual((code, out),
                                     (1, fact_lines() + f"StoreError location: change {old}; not_moved: {name}\n"))
                    self.assertEqual(record.read_bytes(), first)
                    self.assertFalse(new.exists())
                    (old / name).unlink()

            # The old one gone: as holding none; neither holding a store:
            # created, then recorded.
            shutil.rmtree(old)
            code, out, calls = self.command(d, "change", new)
            self.assertEqual((code, out), (0, recorded(LATER_AT, new, moved=True)))
            self.assertIn("create", calls)
            self.assertTrue((new / "store.db").is_file())

            # A new one holding a store, the old one gone: opened, recorded.
            shutil.rmtree(new)
            code, out, calls = self.command(d, "change", held)
            self.assertEqual((code, out), (0, recorded(LATER_AT, held, moved=True)))
            self.assertNotIn("create", calls)
            self.assertEqual(record.read_bytes(), record_of(LATER_AT, held).encode())
            self.unreadable(d, held, record)

    def unreadable(self, d, held, record):
        """The recorded directory unreadable: old_unreadable, where this
        system and user can make one."""
        if sys.platform == "win32" or os.geteuid() == 0:
            return
        before = record.read_bytes()
        held.chmod(0)
        try:
            code, out, _ = self.command(d, "change", held.parent / "next")
        finally:
            held.chmod(0o700)
        self.assertEqual(code, 1)
        self.assertTrue(out.startswith(fact_lines() + f"StoreError location: change {held}; old_unreadable: "), out)
        self.assertEqual(record.read_bytes(), before)

    def test_P16_output(self):
        """init into a directory named with e-diaeresis, the stream ASCII:
        the check's lines, the record, then recorded, '?' for it; exit 0,
        the record whole."""
        root = Path(self.files_root())
        record = root / "config" / "location.json"
        target = root / "caf\u00eb"
        with self.located() as d:
            d.call("set_encoding", encoding="ascii")
            code, out, _ = self.command(d, "init", target)
            self.assertEqual((code, out), (0, recorded(AT, target).replace("\u00eb", "?")))
            self.assertEqual(record.read_bytes(), record_of(AT, target).encode("utf-8"))
            self.assertTrue((target / "store.db").is_file())
            self.assertEqual(d.call("store_dir"), {"dir": str(target)})
