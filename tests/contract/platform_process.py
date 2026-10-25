"""The platform's cases P10 to P15 and P20, each defined once over an
adapter (contract/platform_cases.py names the binding's part): start, run
and a Child, through the drive's requests (driver/platform_child.hpp).

Each case needs processes, which the fake lacks: it checks that need
first, so on the fake it is not applicable having run nothing, P20's lock
half included. What the fake's programs carry (set_programs: a program
found by its whole argv gives its lines, its err and its code, or hangs,
or floods a stream) is the fake's own cases' (contract/platform_own.py).
The Windows adapter's program, start and run are its module win/process',
and the children its cases start are qiven3-child's, both in package 5;
until then a binding with PROCESSES false checks that start answers
unsupported, and the child half is skipped. P20's lock is the files', its
holder the binding's hold() (contract/platform_files.py): on the Windows
adapter its lock half runs before that skip.
"""

import re

from tests.drive.session import debug_library, has_faults

MEMBER = 64 * 1024
# The fake's calls, its fault points: start's pages and start; read_line's
# pages.
START_CALLS, READ_LINE_CALLS = 2, 1
# How often P10's child is killed on an adapter with processes.
KILLS = 20
# P15's arguments: an empty one, one with spaces and quotes, one with e
# diaeresis.
ARGS = ("", 'a "quoted" word, two  spaces', "ë")
# The cases needing processes, not applicable on the fake.
CASES = ("test_P10_replace_killed", "test_P11_tree", "test_P12_deadline", "test_P13_caps",
         "test_P14_environment", "test_P15_argv", "test_P20_lock")


def named(error, stream):
    """Whether an output error names the stream, out or err, alone."""
    other = "err" if stream == "out" else "out"
    return bool(re.search(rf"\b{stream}\b", error.detail)) and not re.search(rf"\b{other}\b", error.detail)


def programs(case, d, **given):
    """The fake's programs, each by its name: its argv, made by
    set_programs with an absolute argv[0] under the case's files' root; a
    program is its "lines", "err", "code", "hang" and "flood", and "args"
    after argv[0]."""
    argv = {}
    made = []
    for name, program in given.items():
        fields = dict(program)
        argv[name] = [str(case.files_root() / "programs" / f"{name}.exe"), *fields.pop("args", ())]
        made.append({"argv": argv[name], **fields})
    d.call("set_programs", programs=made)
    return argv


def lock_half(case, d):
    """lock held, then released before the deadline; held past it; a
    deadline of 0, one try: acquired; busy; busy, then acquired once the
    holder lets go."""
    path = case.files_root() / "store.lock"
    case.hold(d, lock=3)
    held = d.call("lock", path=str(path))["id"]
    case.assertEqual(case.failure(d.call, "lock_release", id=held + 1).kind, "request")
    d.call("lock_release", id=held)
    case.assertEqual(case.failure(d.call, "lock_release", id=held).kind, "request")
    case.hold(d, lock=-1)
    case.assert_platform_error(case.failure(d.call, "lock", path=str(path), deadline=300), "busy")
    case.assert_platform_error(case.failure(d.call, "lock", path=str(path), deadline=0), "busy")
    case.hold(d, lock=1)
    case.assert_platform_error(case.failure(d.call, "lock", path=str(path), deadline=0), "busy")
    again = d.call("lock", path=str(path), deadline=0)["id"]
    d.call("lock_release", id=again)
    case.nothing_kept(d)


class PlatformProcess:
    PROCESSES = True

    def child_half(self, what):
        """The case's child half, its need met: until the adapter's children
        are written, start answers unsupported and the case skips; on an
        adapter with them, until the half is written, a failure."""
        if not self.PROCESSES:
            with self.drive() as d:
                self.assert_platform_error(self.failure(d.call, "start", argv=["none"]), "unsupported")
            self.skipTest(f"until package 5: {what}: win/process and qiven3-child")
        self.fail(f"the child half is not written: {what}")

    def test_P10_replace_killed(self):
        """A child killed between its write and its rename, 20 times: the old
        bytes whole each time."""
        self.needs("processes")
        self.child_half(f"a child killed between replace's write and its rename, {KILLS} times")

    def test_P11_tree(self):
        """A child starting a grandchild: kill(); a supervisor holding both,
        killed: both gone within 1 s each time."""
        self.needs("processes")
        self.child_half("a grandchild's tree killed, by kill() and by its supervisor's death")

    def test_P12_deadline(self):
        """A child sleeping past its deadline: deadline; the tree gone."""
        self.needs("processes")
        self.child_half("a child sleeping past its deadline, its tree gone")

    def test_P13_caps(self):
        """A child printing past its out cap, then its err cap: output naming
        the stream; the tree gone."""
        self.needs("processes")
        self.child_half("a child printing past its out cap, then its err cap, its tree gone")

    def test_P14_environment(self):
        """A child printing its environment's names, with one caller pair:
        the allowlist's names present in v3's own, and the pair; nothing
        else."""
        self.needs("processes")
        self.child_half("a child printing its environment's names")

    def test_P15_argv(self):
        """An empty argument, one with spaces and quotes, one with e
        diaeresis; a relative argv[0]; a .cmd: each argument as given;
        start twice."""
        self.needs("processes")
        self.child_half("each argument read back by the child; a relative argv[0] and a .cmd, start")

    def test_P20_lock(self):
        """lock held, then released before the deadline; held past it:
        acquired; busy; a deadline of 0, one try (the lock half). Two init
        runs at once, each a child: one recorded, the other chosen (the
        child half)."""
        self.needs("processes")
        with self.drive() as d:
            lock_half(self, d)
        self.child_half("two init runs at once, each a child: one recorded, the other chosen")

    def test_child_memory(self):
        """Each Child request within the member's budget, nothing left on its
        arena; under QIVEN3_FAULTS, start's and read_line's arena
        allocations and the fake's calls failed in turn, each a typed error,
        then done, the calls counted. On the fake, whole; elsewhere the
        child half's."""
        if self.ADAPTER != "fake":
            self.needs("processes")
            self.child_half("the Child requests' budgets and faults on a real child")
        with self.drive() as d:
            self.fake_memory(d)

    def fake_memory(self, d):
        long = "x" * 200
        argv = programs(self, d, talker={"lines": [long] * 400})
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
