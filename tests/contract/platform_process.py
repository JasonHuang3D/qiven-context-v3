"""The platform's cases P10 to P15 and P20, each defined once over an
adapter (contract/platform_cases.py names the binding's part): start, run
and a Child, through the drive's requests (driver/platform_child.hpp).

Each case needs processes, which the fake lacks: on the fake it runs its
fake half first, on the fake's programs (set_programs: a program found by
its whole argv gives its lines, its err and its code, or hangs, or floods
a stream), then is not applicable. Its child half, with a real child, runs
on an adapter with processes. The Windows adapter's program, start and
run are its module win/process', and the children its cases start are
qiven3-child's, both in package 5; until then a binding with PROCESSES
false checks that start answers unsupported, and the child half is
skipped. P20's lock is the files' (STAGE_LOCK, contract/platform_files.py).
"""

import re
import time

from tests.drive.session import debug_library, has_faults

MEMBER = 64 * 1024
# The fake's calls, its fault points: replace's pages, replace, its
# temporary name's random_bytes, the write and the rename; start's pages
# and start; read_line's pages.
REPLACE_CALLS, START_CALLS, READ_LINE_CALLS = 5, 2, 1
# P10's bytes, and how often a child is killed on an adapter with processes.
OLD, NEW = b"the old bytes, whole\n", b"the new bytes\n"
KILLS = 20
# P15's arguments: an empty one, one with spaces and quotes, one with e
# diaeresis.
ARGS = ("", 'a "quoted" word, two  spaces', "\u00eb")


def named(error, stream):
    """Whether an output error names the stream, out or err, alone."""
    other = "err" if stream == "out" else "out"
    return bool(re.search(rf"\b{stream}\b", error.detail)) and not re.search(rf"\b{other}\b", error.detail)


class PlatformProcess:
    PROCESSES = True

    def programs(self, d, **programs):
        """The fake's programs for a case's fake half, each by its name: its
        argv, made by set_programs with an absolute argv[0] under the files'
        root; a program is its "lines", "err", "code", "hang" and "flood",
        and "args" after argv[0]."""
        argv = {}
        made = []
        for name, program in programs.items():
            given = dict(program)
            argv[name] = [str(self.files_root() / "programs" / f"{name}.exe"), *given.pop("args", ())]
            made.append({"argv": argv[name], **given})
        d.call("set_programs", programs=made)
        return argv

    def fake_half(self, half):
        """The case's fake half, half(drive), on the fake alone."""
        if self.ADAPTER == "fake":
            with self.drive() as d:
                half(d)

    def child_half(self, what):
        """The case's child half: not applicable without processes; on an
        adapter with them, until its children are written, a failure."""
        if "processes" not in self.TAGS:
            self.skipTest(f"not applicable: processes: the {self.ADAPTER} adapter lacks it: {what}")
        if not self.PROCESSES:
            with self.drive() as d:
                self.assert_platform_error(self.failure(d.call, "start", argv=["none"]), "unsupported")
            self.skipTest(f"until package 5: {what}: win/process and qiven3-child")
        self.fail(f"the child half is not written: {what}")

    def test_P10_replace_killed(self):
        """A child killed between its write and its rename, 20 times: the old
        bytes whole each time. The fake half, with no child, under
        QIVEN3_FAULTS: each of replace's calls failed in turn, as a kill
        there ends it, the old bytes whole after each; then the new bytes."""
        if has_faults():
            self.fake_half(self.fake_P10)
        self.child_half(f"a child killed between replace's write and its rename, {KILLS} times")

    def fake_P10(self, d):
        target = self.files_root() / "killed.txt"
        target.write_bytes(OLD)
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

    def test_P11_tree(self):
        """A child starting a grandchild: kill(); a supervisor holding both,
        killed: both gone within 1 s each time. The fake half: a child that
        never ends, killed: its stream ends and its wait answers within
        1 s; kill() again, idempotent; the Child ended."""
        self.fake_half(self.fake_P11)
        self.child_half("a grandchild's tree killed, by kill() and by its supervisor's death")

    def fake_P11(self, d):
        argv = self.programs(d, endless={"lines": ["one"], "hang": True})
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

    def test_P12_deadline(self):
        """A child sleeping past its deadline: deadline; the tree gone. The
        fake half: a program that hangs, by run and by start: deadline, and
        deadline again on the next call."""
        self.fake_half(self.fake_P12)
        self.child_half("a child sleeping past its deadline, its tree gone")

    def fake_P12(self, d):
        argv = self.programs(d, sleeper={"lines": ["awake"], "hang": True})
        self.assert_platform_error(self.failure(d.call, "run", argv=argv["sleeper"], deadline=200), "deadline")
        child = d.call("start", argv=argv["sleeper"], deadline=200)["id"]
        self.assertEqual(d.call("child_read_line", id=child), b"awake\n".hex())
        for op in ("child_read_line", "child_read_line", "child_wait"):
            with self.subTest(op=op):
                self.assert_platform_error(self.failure(d.call, op, id=child), "deadline")
        d.call("child_end", id=child)

    def test_P13_caps(self):
        """A child printing past its out cap, then its err cap: output naming
        the stream; the tree gone. The fake half: run at each cap, then a
        byte short of the cap; a program flooding out, then err, by run and
        by start."""
        self.fake_half(self.fake_P13)
        self.child_half("a child printing past its out cap, then its err cap, its tree gone")

    def fake_P13(self, d):
        line = b"0123456789\n"
        complaint = "e" * 16
        argv = self.programs(d, printer={"lines": [line[:-1].decode()], "err": complaint, "code": 7},
                             flood_out={"flood": "out"}, flood_err={"flood": "err"})
        exact = {"out": len(line), "err": len(complaint)}
        ran = d.call("run", argv=argv["printer"], caps=exact)
        self.assertEqual(ran, {"code": 7, "out": line.hex(), "err": complaint.encode().hex()})
        for stream in ("out", "err"):
            with self.subTest(past=stream):
                error = self.failure(d.call, "run", argv=argv["printer"], caps={**exact, stream: exact[stream] - 1})
                self.assert_platform_error(error, "output")
                self.assertTrue(named(error, stream), error)
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

    def test_P14_environment(self):
        """A child printing its environment's names, with one caller pair:
        the allowlist's names present in v3's own, and the pair; nothing
        else. The fake's programs print no environment: no fake half."""
        self.child_half("a child printing its environment's names")

    def test_P15_argv(self):
        """An empty argument, one with spaces and quotes, one with e
        diaeresis; a relative argv[0]; a .cmd: each argument as given;
        start twice. The fake half: the program found by its whole argv,
        each argument as given, by run and by start; an argv one argument
        dropped, then one changed, not found: start."""
        self.fake_half(self.fake_P15)
        self.child_half("each argument read back by the child; a relative argv[0] and a .cmd, start")

    def fake_P15(self, d):
        argv = self.programs(d, echo={"args": ARGS, "lines": ["found"]})
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

    def test_P20_lock(self):
        """lock held, then released before the deadline; held past it:
        acquired; busy; a deadline of 0, one try. Two init runs at once,
        each a child: one recorded, the other chosen (the child half)."""
        path = self.files_root() / "store.lock"
        with self.drive() as d:
            if not self.STAGE_LOCK:
                self.files_unsupported(d, "lock", path=str(path))
            self.hold(d, lock=3)
            held = d.call("lock", path=str(path))["id"]
            self.assertEqual(self.failure(d.call, "lock_release", id=held + 1).kind, "request")
            d.call("lock_release", id=held)
            self.assertEqual(self.failure(d.call, "lock_release", id=held).kind, "request")
            self.hold(d, lock=-1)
            self.assert_platform_error(self.failure(d.call, "lock", path=str(path), deadline=300), "busy")
            self.assert_platform_error(self.failure(d.call, "lock", path=str(path), deadline=0), "busy")
            self.hold(d, lock=1)
            self.assert_platform_error(self.failure(d.call, "lock", path=str(path), deadline=0), "busy")
            again = d.call("lock", path=str(path), deadline=0)["id"]
            d.call("lock_release", id=again)
            self.nothing_kept(d)
        self.child_half("two init runs at once, each a child: one recorded, the other chosen")

    def test_child_memory(self):
        """Each Child request within the member's budget, nothing left on its
        arena; under QIVEN3_FAULTS, start's and read_line's arena
        allocations and the fake's calls failed in turn, each a typed error,
        then done, the calls counted. On the fake, whole; elsewhere the
        child half's."""
        if self.ADAPTER != "fake":
            self.child_half("the Child requests' budgets and faults on a real child")
        self.fake_half(self.fake_memory)

    def fake_memory(self, d):
        long = "x" * 200
        argv = self.programs(d, talker={"lines": [long] * 400})
        child = d.call("start", argv=argv["talker"])["id"]
        requests = (("start", {"argv": argv["talker"]}), ("child_read_line", {"id": child}),
                    ("child_write", {"id": child, "data": "00"}), ("child_close_input", {"id": child}),
                    ("child_wait", {"id": child}), ("child_kill", {"id": child}), ("child_end", {"id": child}))
        for op, args in requests:
            with self.subTest(op=op):
                reply = d.reply(op, **args)
                self.assertTrue(reply["ok"], reply)
                memory = reply["memory"]
                self.assertEqual(memory["budget"], MEMBER, reply)
                self.assertLessEqual(memory["peak"], memory["budget"], reply)
                self.assertEqual((memory["left"], reply["held"]), (0, 0), reply)
        if not has_faults() or debug_library():
            return
        child = d.call("start", argv=argv["talker"])["id"]
        for op, of, args, calls in (("start", "allocation", {"argv": argv["talker"]}, 0),
                                    ("start", "platform", {"argv": argv["talker"]}, START_CALLS),
                                    ("child_read_line", "allocation", {"id": child}, None),
                                    ("child_read_line", "platform", {"id": child}, READ_LINE_CALLS)):
            with self.subTest(op=op, of=of):
                k, reply = d.sweep(op, of, **args)
                self.assertEqual(reply["fault"]["calls"], k - 1, reply)
                # The line's allocations follow the standard library's growth
                # (one on MSVC's, two on libstdc++); the fake's calls do not.
                if calls is None:
                    self.assertGreaterEqual(k - 1, 1, reply)
                else:
                    self.assertEqual(k - 1, calls, reply)
        self.assertEqual(reply["result"], (long + "\n").encode().hex())
