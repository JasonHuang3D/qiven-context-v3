"""The fake adapter's own cases, "own: fake", each defined once over an
adapter (contract/platform_cases.py names the binding's part): on the
fake they pass, on any other adapter they are not applicable.

The fake lacks processes, so P10 to P15 and P20 run nothing on it
(contract/platform_process.py); what its programs (set_programs) and its
lock carry for those cases is checked here, through the drive: its
program's hang, flood and exit code reach the caller as deadline, output
and the code; replace failed at each of its calls, as a kill there would
end it; a killed program ended; a program found by its whole argv alone;
start and run refusing a relative argv[0], a .cmd and a .bat; the lock
held and released.
"""

import time

from tests.contract.platform_process import ARGS, NEW, OLD, lock_half, named, programs
from tests.drive.session import has_faults

# replace's calls, its fault points: its pages, replace, its temporary
# name's random_bytes, the write and the rename.
REPLACE_CALLS = 5
# The fake's refusals: each argv[0] one of its programs, so the refusal alone
# keeps it from running, then its detail.
NOT_ABSOLUTE, NO_SHELL = "argv[0] is not absolute", "argv[0] is a .cmd or a .bat: no shell runs it"
CASES = ("test_own_replace_killed", "test_own_kill_ended", "test_own_hang_deadline", "test_own_flood_output",
         "test_own_exit_code", "test_own_argv_found", "test_own_argv_refused", "test_own_lock")


class PlatformOwn:
    def test_own_replace_killed(self):
        """Under QIVEN_FAULTS, each of replace's calls failed in turn, as a
        child killed there ends it: PlatformError, the old bytes whole
        after each; then the new bytes, nothing kept (P10's on the fake)."""
        self.needs("own: fake")
        if not has_faults():
            self.skipTest("not applicable: built without QIVEN_FAULTS")
        target = self.files_root() / "killed.txt"
        target.write_bytes(OLD)
        with self.drive() as d:
            for k in range(1, 100):
                reply = d.reply("replace", path=str(target), data=NEW.hex(), fault={"k": k, "of": "platform"})
                if not reply["fault"]["fired"]:
                    break
                self.assertEqual((reply["ok"], reply["error"]["kind"]), (False, "platform"), reply)
                self.assertEqual(target.read_bytes(), OLD, k)
            self.assertTrue(reply["ok"], reply)
            self.assertEqual(k, REPLACE_CALLS + 1, reply)
            self.assertEqual(target.read_bytes(), NEW)
            self.assertEqual(d.call("kept"), 0)

    def test_own_kill_ended(self):
        """A program that never ends, killed: its stream ends and its wait
        answers 1 within 1 s; kill() again, idempotent; the Child ended, a
        request naming it refused (P11's on the fake)."""
        self.needs("own: fake")
        with self.drive() as d:
            argv = programs(self, d, endless={"lines": ["one"], "hang": True})
            child = d.call("start", argv=argv["endless"])["id"]
            self.assertEqual(d.call("child_read_line", id=child), b"one\n".hex())
            began = time.monotonic()
            d.call("child_kill", id=child)
            self.assertIsNone(d.call("child_read_line", id=child))
            self.assertEqual(d.call("child_wait", id=child), 1)
            self.assertLess(time.monotonic() - began, 1.0)
            d.call("child_kill", id=child)
            self.assertEqual(d.call("child_wait", id=child), 1)
            d.call("child_end", id=child)
            self.assertEqual(self.failure(d.call, "child_wait", id=child).kind, "request")

    def test_own_hang_deadline(self):
        """A program that hangs, by run and by start: deadline, and deadline
        again on each next call (P12's on the fake)."""
        self.needs("own: fake")
        with self.drive() as d:
            argv = programs(self, d, sleeper={"lines": ["awake"], "hang": True})
            self.assert_platform_error(self.failure(d.call, "run", argv=argv["sleeper"], deadline=200), "deadline")
            child = d.call("start", argv=argv["sleeper"], deadline=200)["id"]
            self.assertEqual(d.call("child_read_line", id=child), b"awake\n".hex())
            for op in ("child_read_line", "child_read_line", "child_wait"):
                with self.subTest(op=op):
                    self.assert_platform_error(self.failure(d.call, op, id=child), "deadline")
            d.call("child_end", id=child)

    def test_own_flood_output(self):
        """A program flooding out, then err, by run and by start: output
        naming the stream, from run, read_line and wait (P13's on the
        fake)."""
        self.needs("own: fake")
        with self.drive() as d:
            argv = programs(self, d, flood_out={"flood": "out"}, flood_err={"flood": "err"})
            for stream in ("out", "err"):
                with self.subTest(flood=stream):
                    error = self.failure(d.call, "run", argv=argv[f"flood_{stream}"])
                    self.assert_platform_error(error, "output")
                    self.assertTrue(named(error, stream), error)
                    child = d.call("start", argv=argv[f"flood_{stream}"])["id"]
                    for op in ("child_read_line", "child_wait"):
                        error = self.failure(d.call, op, id=child)
                        self.assert_platform_error(error, "output")
                        self.assertTrue(named(error, stream), error)
                    d.call("child_end", id=child)

    def test_own_exit_code(self):
        """A program printing a line and its err, exiting 7, run at each cap
        exactly: the code, out and err as given; a byte short of the out
        cap, then of the err cap: output naming the stream (P13's on the
        fake)."""
        self.needs("own: fake")
        line = b"0123456789\n"
        complaint = "e" * 16
        with self.drive() as d:
            argv = programs(self, d, printer={"lines": [line[:-1].decode()], "err": complaint, "code": 7})
            exact = {"out": len(line), "err": len(complaint)}
            ran = d.call("run", argv=argv["printer"], caps=exact)
            self.assertEqual(ran, {"code": 7, "out": line.hex(), "err": complaint.encode().hex()})
            for stream in ("out", "err"):
                with self.subTest(past=stream):
                    error = self.failure(d.call, "run", argv=argv["printer"], caps={**exact, stream: exact[stream] - 1})
                    self.assert_platform_error(error, "output")
                    self.assertTrue(named(error, stream), error)

    def test_own_argv_found(self):
        """A program found by its whole argv, an empty argument, one with
        spaces and quotes and one with e diaeresis among it, by run and by
        start; that argv one argument dropped, then one changed: not found,
        start (P15's on the fake)."""
        self.needs("own: fake")
        with self.drive() as d:
            argv = programs(self, d, echo={"args": ARGS, "lines": ["found"]})
            self.assertEqual(d.call("run", argv=argv["echo"])["out"], b"found\n".hex())
            child = d.call("start", argv=argv["echo"])["id"]
            self.assertEqual(d.call("child_read_line", id=child), b"found\n".hex())
            d.call("child_end", id=child)
            dropped = [argv["echo"][0], *ARGS[1:]]
            changed = [argv["echo"][0], "", ARGS[1].replace('"', "'"), ARGS[2]]
            for other in (dropped, changed):
                with self.subTest(argv=other):
                    self.assert_platform_error(self.failure(d.call, "run", argv=other), "start")
                    self.assert_platform_error(self.failure(d.call, "start", argv=other), "start")

    def test_own_argv_refused(self):
        """start and run of a relative argv[0], a .cmd and a .bat, each one
        of the fake's programs: PlatformError start, naming why, by start
        and by run; the absolute .exe beside them runs."""
        self.needs("own: fake")
        root = self.files_root()
        refused = (("talker.exe", NOT_ABSOLUTE), ("bin/talker.exe", NOT_ABSOLUTE),
                   (str(root / "script.cmd"), NO_SHELL), (str(root / "script.bat"), NO_SHELL),
                   (str(root / "SCRIPT.BAT"), NO_SHELL))
        talker = str(root / "talker.exe")
        with self.drive() as d:
            made = [{"argv": [first], "lines": ["ran"]} for first, _ in refused]
            d.call("set_programs", programs=[*made, {"argv": [talker], "lines": ["ran"]}])
            for first, detail in refused:
                for op in ("start", "run"):
                    with self.subTest(argv0=first, op=op):
                        error = self.failure(d.call, op, argv=[first])
                        self.assert_platform_error(error, "start")
                        self.assertIn(detail, error.detail)
            self.assertEqual(d.call("run", argv=[talker])["out"], b"ran\n".hex())

    def test_own_lock(self):
        """The fake's lock held, then released before the deadline; held
        past it; a deadline of 0, one try: acquired; busy; busy, then
        acquired once the holder lets go; nothing kept (P20's lock half on
        the fake)."""
        self.needs("own: fake")
        with self.drive() as d:
            lock_half(self, d)
