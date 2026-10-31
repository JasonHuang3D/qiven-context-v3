"""The platform's cases P3 to P8 and P16, each defined once over an adapter
(contract/platform_cases.py names the binding's part): location's commands,
init, change and show, run by its main on the adapter; store_dir, the live
store's directory as the commands that open it read it, through the
drive.

Each drive's configuration directory is "<files root>/config". On the
fake the commands run through the drive's location request, its clock set
to CLOCK_US, its facts each off on volume V: unless the case changes them,
and the commands' stream is the fake's, as the request's "out", with the
platform calls each made. A binding with SYSTEM_LOCATION true runs them as
the binary, qiven3 location ..., with the drive's configuration directory,
its standard output to a pipe kept apart from the drive's replies: the
system's own facts and clock, set by no request, and no calls recorded.
The cases read the facts first, giving --accept where they warn, and read
the record's "at", the system's clock within 1 s of the case's around the
command (P24's bound); the commands' stream is the adapter's to a pipe,
the ANSI code page with each LF as CRLF; the files root is resolved, as
the commands resolve their directories. The fake's own requests' parts
(P5's synced folder and links, P6's warnings, P16's ASCII stream, the
calls, the arena's peak) are not applicable there, each by its reason.
"""

import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

from tests.drive import binaries

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


def check_lines(facts):
    """The check's lines for a facts reply: its volume, each condition's
    state and reason."""
    lines = []
    for key, (name, why, off, look) in CONDITIONS.items():
        state, reason = facts[key]["state"], facts[key]["reason"]
        head = f"{name}: {state} ({reason}), volume {facts['volume']}. "
        lines.append(head + off + look if state == "off" else f"warning: {head}{why}{look}")
    return "".join(lines)


def fact_lines(states=None):
    """The fake's check's lines, each condition off but those states names."""
    states = states or {}
    return check_lines({"volume": "V:", **{key: {"state": states.get(key, "off"), "reason": "fake"}
                                          for key in CONDITIONS}})


def record_of(at, directory):
    """The record's canonical line."""
    return json.dumps({"at": at, "dir": str(directory)}, ensure_ascii=False, separators=(",", ":")) + "\n"


def recorded(at, directory, moved=False, lines=None):
    """A recorded command's output: the check (the fake's unless lines),
    the move's line where the directory changed, the record, then
    recorded."""
    lines = fact_lines() if lines is None else lines
    return lines + (MOVED if moved else "") + record_of(at, directory) + f"recorded {directory}\n"


def utc_now():
    """This system's clock, UTC, truncated to milliseconds as the record's."""
    now = datetime.now(timezone.utc)
    return now.replace(microsecond=now.microsecond // 1000 * 1000)


class PlatformLocation:
    SYSTEM_LOCATION = False

    def location_root(self):
        """The files root, resolved where the system's commands resolve."""
        root = self.files_root()
        return Path(os.path.realpath(root)) if self.SYSTEM_LOCATION else Path(root)

    def located(self, config=None):
        """A drive for location's commands; on the fake its facts each off,
        its clock at CLOCK_US."""
        self.needs("files")
        config = str(config or self.location_root() / "config")
        d = self.drive(config=config)
        if self.SYSTEM_LOCATION:
            d.location_config = config
            return d
        try:
            d.call("set_facts", **OFF)
            d.call("set_clock", us=CLOCK_US)
        except BaseException:
            d.close(check=False)
            raise
        return d

    def fake_only(self, what):
        """Where the facts and clock are the system's: not applicable."""
        if self.SYSTEM_LOCATION:
            self.skipTest(f"not applicable: {what}, set by request")

    def stream_text(self, out):
        """A command's output bytes as text: the fake's stream's UTF-8; the
        adapter's to a pipe, the ANSI code page, each LF as CRLF."""
        if not self.SYSTEM_LOCATION:
            return out.decode("utf-8")
        text = out.decode("mbcs")
        self.assertEqual(text.count("\n"), text.count("\r\n"), text)
        return text.replace("\r\n", "\n")

    def command(self, d, *argv, env=None):
        """The command's exit status, its output and the platform calls it
        made. On the fake, through the drive's location request, its
        arena's peak within its budget; on the system's, the binary,
        qiven3 location ..., under env (None: this process's), with the
        drive's configuration directory unless argv names one, its
        standard error empty and no calls."""
        argv = [str(a) for a in argv]
        if self.SYSTEM_LOCATION:
            config = [] if "--config" in argv else ["--config", d.location_config]
            ran = subprocess.run([str(binaries.path("qiven3")), "location", *argv, *config], capture_output=True,
                                 env=env, timeout=binaries.TIMEOUT, check=False)
            self.assertEqual(ran.stderr, b"", ran)
            return ran.returncode, self.stream_text(ran.stdout), None
        result = d.call("location", argv=argv)
        self.assertLessEqual(result["peak"], BUDGET, result)
        return result["code"], self.stream_text(bytes.fromhex(result["out"])), result["calls"]

    def checked(self, d, directory):
        """The check's lines on directory, and the arguments that accept
        them: on the fake each off; else the system's facts, read first,
        with --accept where they warn."""
        if not self.SYSTEM_LOCATION:
            return fact_lines(), ()
        lines = check_lines(d.call("facts", path=str(directory)))
        return lines, ("--accept",) if "warning: " in lines else ()

    def called(self, calls, name, made=True):
        """On the fake, whether the command called name; no calls are
        recorded on the system's drive."""
        if not self.SYSTEM_LOCATION:
            (self.assertIn if made else self.assertNotIn)(name, calls)

    def stamp(self, record, given, since):
        """The record's "at": the fake's clock, given; the system's, read
        from the record, within 1 s of the case's clock from since to
        now."""
        if not self.SYSTEM_LOCATION:
            return given
        at = json.loads(record.read_bytes())["at"]
        moment = datetime.strptime(at, "%Y-%m-%dT%H:%M:%S.%f%z")
        self.assertEqual(moment.strftime("%Y-%m-%dT%H:%M:%S.") + f"{moment.microsecond // 1000:03d}Z", at)
        second = timedelta(seconds=1)
        self.assertTrue(since - second <= moment <= utc_now() + second, (at, since))
        return at

    def refusal(self, d):
        """store_dir's StoreError: its class, code, detail, operation, path."""
        refused = d.call("store_dir")["refused"]
        return refused["class"], refused["code"], refused["detail"], refused["operation"], refused["path"]

    def test_P3_record(self):
        """init into an empty directory; the record read back; records with
        a key added, dir relative, a control character, a duplicate key:
        each refused, StoreError location config."""
        root = self.location_root()
        store, record = root / "store", root / "config" / "location.json"
        with self.located() as d:
            lines, accept = self.checked(d, store)
            since = utc_now()
            code, out, _ = self.command(d, "init", store, *accept)
            at = self.stamp(record, AT, since)
            self.assertEqual((code, out), (0, recorded(at, store, lines=lines)))
            self.assertEqual(record.read_bytes(), record_of(at, store).encode())
            self.assertEqual(d.call("store_dir"), {"dir": str(store)})
            self.assertEqual(self.command(d, "show")[:2], (0, recorded(at, store, lines=lines)))

            dir_text = json.dumps(str(store))[1:-1]
            forms = {
                "a key added": f'{{"at":"{at}","dir":"{dir_text}","x":1}}\n',
                "dir relative": f'{{"at":"{at}","dir":"store"}}\n',
                "a control character": f'{{"at":"{at}","dir":"{dir_text}\\u0001x"}}\n',
                "a duplicate key": f'{{"at":"{at}","dir":"{dir_text}","dir":"{dir_text}"}}\n',
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
        config = self.location_root() / "config"
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
        no file made. The synced folder and the links are the fake's, set
        by request: on the system's drive P22 has them."""
        root = self.location_root()
        record = root / "config" / "location.json"
        repo, worktree, cloud = root / "repo", root / "wt", root / "cloud"
        (repo / ".git").mkdir(parents=True)
        worktree.mkdir()
        (worktree / ".git").write_bytes(b"gitdir: elsewhere\n")
        cloud.mkdir()
        targets = (
            (repo / "s", repo / "s", "checkout"),
            (worktree / "s", worktree / "s", "checkout"),
        ) + (() if self.SYSTEM_LOCATION else (
            (cloud / "s", cloud / "s", "synced"),
            (root / "to-repo" / "s", repo / "s", "checkout"),
            (root / "to-cloud" / "s", cloud / "s", "synced"),
        ))
        with self.located() as d:
            if not self.SYSTEM_LOCATION:
                d.call("set_synced", folders=[[str(cloud), "fake drive"]])
                d.call("set_links", links=[[str(root / "to-repo"), str(repo)], [str(root / "to-cloud"), str(cloud)]])
            for op in ("init", "change"):
                if op == "change":
                    accept = self.checked(d, root / "store")[1]
                    self.assertEqual(self.command(d, "init", root / "store", *accept)[0], 0)
                before = record.read_bytes() if record.exists() else None
                for given, resolved, detail in targets:
                    with self.subTest(op=op, target=str(given.relative_to(root))):
                        code, out, calls = self.command(d, op, given, "--accept")
                        self.assertEqual((code, out), (1, f"StoreError location: guard {resolved}; {detail}\n"))
                        self.called(calls, "facts", made=False)
                        self.called(calls, "create", made=False)
                        self.assertEqual(record.read_bytes() if record.exists() else None, before)
                        self.assertFalse(resolved.exists())

    def test_P6_warnings(self):
        """Each condition on, then unknown, and BitLocker locked, without
        and then with --accept: one line per condition, saying how to look;
        exit 4 and nothing recorded, then recorded."""
        self.fake_only("the warnings are the fake's facts")
        root = self.location_root()
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
        root = self.location_root()
        config = root / "config"
        record = config / "location.json"
        made, empty = root / "made", root / "empty"
        with self.located() as d:
            self.assertFalse(config.exists())
            lines, accept = self.checked(d, made)
            since = utc_now()
            code, out, calls = self.command(d, "init", made, *accept)
            self.assertEqual((code, out), (0, recorded(self.stamp(record, AT, since), made, lines=lines)))
            self.assertTrue(config.is_dir())
            self.called(calls, "create")
            self.assertTrue((made / "store.db").is_file())

            before = record.read_bytes()
            self.assertEqual(self.command(d, "init", root / "again")[:2],
                             (1, f"StoreError location: init {record}; chosen\n"))
            self.assertEqual(record.read_bytes(), before)
            self.assertFalse((root / "again").exists())

            record.unlink()
            since = utc_now()
            code, out, calls = self.command(d, "init", made, *accept)
            self.assertEqual((code, out), (0, recorded(self.stamp(record, AT, since), made, lines=lines)))
            self.called(calls, "create", made=False)

            record.unlink()
            empty.mkdir()
            lines, accept = self.checked(d, empty)
            since = utc_now()
            code, out, calls = self.command(d, "init", empty, *accept)
            self.assertEqual((code, out), (0, recorded(self.stamp(record, AT, since), empty, lines=lines)))
            self.called(calls, "create")
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
                self.called(calls, "create", made=False)
                self.assertFalse(target.exists())
        self.assertFalse(lost.parent.exists())
        self.assertEqual(a_file.read_bytes(), b"kept")

    def test_P8_change(self):
        """change to the same directory; the old one still holding store.db,
        then map-marks.json alone; an old one gone, then unreadable; a new
        one holding a store; neither holding one."""
        root = self.location_root()
        record = root / "config" / "location.json"
        old, new, held = root / "old", root / "new", root / "held"
        with self.located(config=root / "elsewhere") as e:
            self.assertEqual(self.command(e, "init", held, *self.checked(e, held)[1])[0], 0)
        with self.located() as d:
            lines, accept = self.checked(d, old)
            since = utc_now()
            self.assertEqual(self.command(d, "init", old, *accept)[0], 0)
            at = self.stamp(record, AT, since)
            first = record.read_bytes()
            if not self.SYSTEM_LOCATION:
                d.call("set_clock", us=LATER_US)
            self.assertEqual(self.command(d, "change", old, *accept)[:2], (0, recorded(at, old, lines=lines)))
            self.assertEqual(record.read_bytes(), first)

            lines, accept = self.checked(d, new)
            for name in LEFT:
                with self.subTest(left=name):
                    # store.db is init's store; map-marks.json alone after it.
                    if name != "store.db":
                        (old / name).write_bytes(b"x")
                    code, out, _ = self.command(d, "change", new, *accept)
                    self.assertEqual((code, out),
                                     (1, lines + f"StoreError location: change {old}; not_moved: {name}\n"))
                    self.assertEqual(record.read_bytes(), first)
                    self.assertFalse(new.exists())
                    (old / name).unlink()

            # The old one gone: as holding none; neither holding a store:
            # created, then recorded.
            shutil.rmtree(old)
            since = utc_now()
            code, out, calls = self.command(d, "change", new, *accept)
            self.assertEqual((code, out), (0, recorded(self.stamp(record, LATER_AT, since), new, True, lines)))
            self.called(calls, "create")
            self.assertTrue((new / "store.db").is_file())

            # A new one holding a store, the old one gone: opened, recorded.
            shutil.rmtree(new)
            lines, accept = self.checked(d, held)
            since = utc_now()
            code, out, calls = self.command(d, "change", held, *accept)
            at = self.stamp(record, LATER_AT, since)
            self.assertEqual((code, out), (0, recorded(at, held, True, lines)))
            self.called(calls, "create", made=False)
            self.assertEqual(record.read_bytes(), record_of(at, held).encode())
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
        """init into a directory named with e-diaeresis, the stream ASCII on
        the fake, the ANSI code page on the system's: the check's lines,
        the record, then recorded, '?' for it where the stream lacks it;
        exit 0, the record whole."""
        root = self.location_root()
        record = root / "config" / "location.json"
        target = root / "caf\N{LATIN SMALL LETTER E WITH DIAERESIS}"
        with self.located() as d:
            if not self.SYSTEM_LOCATION:
                d.call("set_encoding", encoding="ascii")
            lines, accept = self.checked(d, target)
            since = utc_now()
            code, out, _ = self.command(d, "init", target, *accept)
            at = self.stamp(record, AT, since)
            text = recorded(at, target, lines=lines)
            stream = "mbcs" if self.SYSTEM_LOCATION else "ascii"
            self.assertEqual((code, out), (0, text.encode(stream, "replace").decode(stream)))
            self.assertEqual(record.read_bytes(), record_of(at, target).encode("utf-8"))
            self.assertTrue((target / "store.db").is_file())
            self.assertEqual(d.call("store_dir"), {"dir": str(target)})
