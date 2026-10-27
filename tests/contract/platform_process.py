"""The platform's cases P10 to P15 and P20, each defined once over an
adapter (contract/platform_cases.py names the binding's part): start, run
and a Child, through the drive's requests (driver/platform_child.hpp).

Each case needs processes, which the fake lacks: it checks that need
first, so on the fake it is not applicable having run nothing, P20's lock
half included. What the fake's programs carry (set_programs: a program
found by its whole argv gives its lines, its err and its code, or hangs,
or floods a stream) is the fake's own cases' (contract/platform_own.py).
The Windows adapter's program, start and run are its module win/process',
and the children its cases start are qiven3-child platform's, started
through the drive; a binding with PROCESSES false checks that run refuses
a relative argv[0] (start), and the child half is skipped. P20's lock is
the files', its holder the binding's hold() (contract/platform_files.py),
timed where P20 runs; its child half's two init runs are qiven3-child
platform init's, each location's init on the adapter. P11's supervisor is
a drive holding the tree, killed by the test.

The readings where the text is silent run on every adapter, the fake's
programs echoing their command as the child's forms print it: an empty
cwd is v3's own working directory; a caller's environment pair replaces
the allowlist's entry of the same name; read_line and wait past their
timeout answer deadline and leave the tree running, which the Child's end
stops.
"""

import json
import os
import re
import time

from tests.contract.platform_files import temporaries
from tests.drive import binaries
from tests.drive.session import DriveError, debug_library, has_faults

MEMBER = 64 * 1024
# Each adapter's calls, its fault points: on the fake start's pages and
# start, read_line's pages; on the Windows adapter start's 24 Win32 calls,
# none in read_line.
CALLS = {"fake": (2, 1), "windows": (24, 0)}
# How often P10's child is killed on an adapter with processes; its bytes.
KILLS = 20
OLD, NEW = b"the old bytes, whole\n", b"the new bytes\n"
# A child's environment: the allowlist's names v3's own holds.
ALLOWLIST = ("SystemRoot", "SystemDrive", "TEMP", "TMP")
# P11's bound in seconds; a tree's sleep, in ms, longer than any case;
# P12's deadlines, by run and by start, in ms; P13's caps and its flood.
GONE, LONG = 1.0, 60000
DEADLINE, TREE_DEADLINE = 1000, 6000
CAP, FLOOD = 4096, 100000
# The child's lines: a timeout, in ms, past a slow start here; one a
# sleeping child is past.
LINE_TIMEOUT, SHORT = 30000, 200
# P20's lock half, timed: the holder's 3 counts of 50 ms; the deadline
# past which busy, its slack; in seconds.
HELD, PAST, SLACK = 0.15, 0.3, 1.0
# The adapter's clock's tick, which a deadline may end short of.
TICK = 0.016
# P15's arguments: an empty one, one with spaces and quotes, one with e
# diaeresis.
ARGS = ("", 'a "quoted" word, two  spaces', "\u00eb")
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


def lock_half(case, d, timed=False):
    """lock held, then released before the deadline; held past it; a
    deadline of 0, one try: acquired; busy; busy, then acquired once the
    holder lets go. Timed, where the deadlines are kept: acquired after
    the holder's time and before the deadline, busy no sooner than its
    deadline, and at 0 without waiting."""
    path = case.files_root() / "store.lock"
    case.hold(d, lock=3)
    began = time.monotonic()
    held = d.call("lock", path=str(path))["id"]
    waited = time.monotonic() - began
    case.assertEqual(case.failure(d.call, "lock_release", id=held + 1).kind, "request")
    d.call("lock_release", id=held)
    case.assertEqual(case.failure(d.call, "lock_release", id=held).kind, "request")
    case.hold(d, lock=-1)
    began = time.monotonic()
    case.assert_platform_error(case.failure(d.call, "lock", path=str(path), deadline=int(PAST * 1000)), "busy")
    past = time.monotonic() - began
    began = time.monotonic()
    case.assert_platform_error(case.failure(d.call, "lock", path=str(path), deadline=0), "busy")
    once = time.monotonic() - began
    if timed:
        case.assertTrue(HELD - TICK <= waited < 5.0, waited)
        case.assertTrue(PAST - TICK <= past < PAST + SLACK, past)
        case.assertLess(once, PAST, once)
    case.hold(d, lock=1)
    case.assert_platform_error(case.failure(d.call, "lock", path=str(path), deadline=0), "busy")
    again = d.call("lock", path=str(path), deadline=0)["id"]
    d.call("lock_release", id=again)
    case.nothing_kept(d)


class PlatformProcess:
    PROCESSES = True

    def child_half(self, what):
        """The case's child half, its need met: until the adapter's children
        are written, run refuses a relative argv[0] (start) and the case
        skips; on an adapter with them, the case goes on to its half."""
        if not self.PROCESSES:
            with self.drive() as d:
                self.assert_platform_error(self.failure(d.call, "run", argv=["none"]), "start")
            self.skipTest(f"until package 5: {what}: qiven3-child")

    def child(self, *args):
        """The argv of qiven3-child's platform form args, its argv[0] the
        build's."""
        return [str(binaries.path("qiven3-child")), "platform", *map(str, args)]

    def lines(self, d, child, n):
        """The child's next n lines, each without its LF."""
        return [bytes.fromhex(d.call("child_read_line", id=child, timeout=LINE_TIMEOUT)).decode().rstrip("\n")
                for _ in range(n)]

    def tree(self, d, child):
        """A tree child's grandchild's id and its own, from its two lines."""
        grandchild, own = self.lines(d, child, 2)
        self.assertTrue(grandchild.startswith("grandchild ") and own.startswith("pid "), (grandchild, own))
        return int(grandchild.split()[1]), int(own.split()[1])

    def assert_gone(self, d, *pids):
        """Each process ended within GONE seconds of its check (the gone
        child's own wait)."""
        for pid in pids:
            ran = d.call("run", argv=self.child("gone", pid, int(GONE * 1000)))
            self.assertEqual(bytes.fromhex(ran["out"]), b"gone\n", pid)

    def failed_line(self, d, child):
        """The error the child's lines end in, read through."""
        for _ in range(FLOOD // 64 + 8):
            try:
                if d.call("child_read_line", id=child, timeout=LINE_TIMEOUT) is None:
                    break
            except DriveError as caught:
                return caught
        return self.fail("the child's lines ended with no error")

    def written(self, directory, size):
        """Waits for replace's temporary file in directory to hold size
        bytes, then two of its rename's tries more."""
        until = time.monotonic() + LINE_TIMEOUT / 1000
        while time.monotonic() < until:
            left = temporaries(directory)
            if left and (directory / left[0]).stat().st_size == size:
                time.sleep(0.1)
                return
            time.sleep(0.005)
        self.fail(f"no temporary file of {size} bytes in {directory}")

    def test_P10_replace_killed(self):
        """A child killed between its write and its rename, 20 times: the old
        bytes whole each time. The child's replace is the adapter's, its
        target held by a reader of the child's own so that the rename waits;
        the kill comes once the temporary file holds the new bytes, which it
        leaves, alone."""
        self.needs("processes")
        self.child_half(f"a child killed between replace's write and its rename, {KILLS} times")
        target = self.files_root() / "killed.txt"
        target.write_bytes(OLD)
        with self.drive() as d:
            for k in range(KILLS):
                with self.subTest(kill=k):
                    child = d.call("start", argv=self.child("replace", target, NEW.hex(), LONG))["id"]
                    self.assertEqual(self.lines(d, child, 1), ["held"])
                    self.written(target.parent, len(NEW))
                    d.call("child_kill", id=child)
                    self.assertEqual(d.call("child_wait", id=child), 1)
                    d.call("child_end", id=child)
                    self.assertEqual(target.read_bytes(), OLD)
                    left = temporaries(target.parent)
                    self.assertEqual(len(left), 1, left)
                    self.assertEqual((target.parent / left[0]).read_bytes(), NEW)
                    (target.parent / left[0]).unlink()

    def test_P11_tree(self):
        """A child starting a grandchild: kill(); a supervisor holding both,
        killed: both gone within 1 s each time. The supervisor is a second
        drive holding the child's tree, killed by the test."""
        self.needs("processes")
        self.child_half("a grandchild's tree killed, by kill() and by its supervisor's death")
        with self.drive() as d:
            child = d.call("start", argv=self.child("tree", LONG))["id"]
            grandchild, own = self.tree(d, child)
            began = time.monotonic()
            d.call("child_kill", id=child)
            self.assertEqual(d.call("child_wait", id=child), 1)
            self.assertLess(time.monotonic() - began, GONE)
            self.assert_gone(d, grandchild, own)
            d.call("child_end", id=child)
            supervisor = self.drive()
            try:
                held = supervisor.call("start", argv=self.child("tree", LONG))["id"]
                grandchild, own = self.tree(supervisor, held)
                supervisor.process.kill()
                supervisor.process.wait(timeout=binaries.TIMEOUT)
            finally:
                supervisor.close(check=False)
            self.assert_gone(d, grandchild, own)

    def test_P12_deadline(self):
        """A child sleeping past its deadline: deadline, by run no sooner
        than it; by start, deadline at each next call; the tree gone."""
        self.needs("processes")
        self.child_half("a child sleeping past its deadline, its tree gone")
        with self.drive() as d:
            began = time.monotonic()
            error = self.failure(d.call, "run", argv=self.child("sleep", LONG), deadline=DEADLINE)
            self.assert_platform_error(error, "deadline")
            self.assertGreaterEqual(time.monotonic() - began, DEADLINE / 1000 - TICK)
            child = d.call("start", argv=self.child("tree", LONG), deadline=TREE_DEADLINE)["id"]
            grandchild, own = self.tree(d, child)
            for op in ("child_read_line", "child_read_line", "child_wait"):
                with self.subTest(op=op):
                    error = self.failure(d.call, op, id=child, timeout=LINE_TIMEOUT)
                    self.assert_platform_error(error, "deadline")
            self.assert_gone(d, grandchild, own)
            d.call("child_end", id=child)

    def test_P13_caps(self):
        """A child printing past its out cap, then its err cap: output naming
        the stream, by run, then by start from read_line and wait; the tree
        gone."""
        self.needs("processes")
        self.child_half("a child printing past its out cap, then its err cap, its tree gone")
        caps = {"out": CAP, "err": CAP}
        with self.drive() as d:
            for stream in ("out", "err"):
                with self.subTest(stream=stream):
                    error = self.failure(d.call, "run", argv=self.child("flood", stream, FLOOD, LONG), caps=caps)
                    self.assert_platform_error(error, "output")
                    self.assertTrue(named(error, stream), error)
                    child = d.call("start", argv=self.child("tree", LONG, stream, FLOOD), caps=caps)["id"]
                    grandchild, own = self.tree(d, child)
                    d.call("child_write", id=child, data=b"go\n".hex())
                    for error in (self.failed_line(d, child), self.failure(d.call, "child_wait", id=child)):
                        self.assert_platform_error(error, "output")
                        self.assertTrue(named(error, stream), error)
                    self.assert_gone(d, grandchild, own)
                    d.call("child_end", id=child)

    def test_P14_environment(self):
        """A child printing its environment's names, with one caller pair:
        the allowlist's names present in v3's own, and the pair; nothing
        else."""
        self.needs("processes")
        self.child_half("a child printing its environment's names")
        name, value = "QIVEN3_PAIR", "a value"
        with self.drive() as d:
            ran = d.call("run", argv=self.child("env", name), env=[[name, value]])
        lines = bytes.fromhex(ran["out"]).decode().splitlines()
        own = [n for n in ALLOWLIST if os.environ.get(n) is not None]
        self.assertEqual(sorted(n.casefold() for n in lines[:-1]), sorted(n.casefold() for n in [*own, name]), lines)
        self.assertEqual(lines[-1], f"{name}={value}")

    def test_P15_argv(self):
        """An empty argument, one with spaces and quotes, one with e
        diaeresis; a relative argv[0]; a .cmd: each argument as given, by
        run and by start; start twice, by start and by run."""
        self.needs("processes")
        self.child_half("each argument read back by the child; a relative argv[0] and a .cmd, start")
        given = [arg.encode().hex() for arg in ARGS]
        script = self.files_root() / "script.cmd"
        script.write_bytes(b"@echo ran\r\n")
        with self.drive() as d:
            self.assertEqual(bytes.fromhex(d.call("run", argv=self.child("argv", *ARGS))["out"]).decode().splitlines(),
                             given)
            child = d.call("start", argv=self.child("argv", *ARGS))["id"]
            self.assertEqual(self.lines(d, child, len(ARGS)), given)
            d.call("child_end", id=child)
            for argv in (["qiven3-child", "platform", "sleep", "0"], [str(script)]):
                for op in ("start", "run"):
                    with self.subTest(argv0=argv[0], op=op):
                        self.assert_platform_error(self.failure(d.call, op, argv=argv), "start")

    def test_P20_lock(self):
        """lock held, then released before the deadline; held past it:
        acquired; busy; a deadline of 0, one try (the lock half), timed.
        Two init runs at once, each a child: one recorded, the other chosen
        (the child half). Each child, over one configuration directory and
        a directory of its own, says ready, then runs init once its input
        ends, the two inputs ended together: one exits 0, recorded, the
        record naming its directory, which holds the store; the other exits
        1 on one line naming chosen, its directory left empty."""
        self.needs("processes")
        with self.drive() as d:
            lock_half(self, d, timed=True)
        self.child_half("two init runs at once, each a child: one recorded, the other chosen")
        config = self.files_root() / "inits"
        chosen = [self.files_root() / f"init-{k}" for k in range(2)]
        for each in chosen:
            each.mkdir()
        with self.drive() as d:
            children = [d.call("start", argv=self.child("init", config, each), deadline=LONG)["id"]
                        for each in chosen]
            for child in children:
                self.assertEqual(self.lines(d, child, 1), ["ready"])
            for child in children:
                d.call("child_close_input", id=child)
            codes = [d.call("child_wait", id=child, timeout=LINE_TIMEOUT) for child in children]
            said = [self.rest(d, child) for child in children]
            for child in children:
                d.call("child_end", id=child)
        self.assertEqual(sorted(codes), [0, 1], said)
        won = codes.index(0)
        lost = 1 - won
        self.assertTrue(said[won][-1].startswith("recorded"), said)
        self.assertEqual(len(said[lost]), 1, said)
        self.assertIn("chosen", said[lost][0])
        record = json.loads((config / "location.json").read_bytes())
        self.assertTrue(os.path.samefile(record["dir"], chosen[won]), record)
        self.assertTrue((chosen[won] / "store.db").is_file())
        self.assertEqual(list(chosen[lost].iterdir()), [])

    def rest(self, d, child):
        """The child's lines left, each without its LF, to its output's end."""
        out = []
        while (line := d.call("child_read_line", id=child, timeout=LINE_TIMEOUT)) is not None:
            out.append(bytes.fromhex(line).decode().rstrip("\n"))
        return out

    def echo(self, d, form, *args):
        """The argv of a program printing its cwd or its environment, as
        qiven3-child platform's form of that name: on the fake its program
        echoing it (set_programs' "echo"), elsewhere the child."""
        if self.ADAPTER == "fake":
            return programs(self, d, echo={"echo": form, "args": args})["echo"]
        self.child_half(f"a child printing its {form}")
        return self.child(form, *args)

    def test_cwd(self):
        """An empty cwd: the program runs in v3's own working directory; a
        given one: in it; by run and by start, on every adapter."""
        own, given = self.files_root() / "own", self.files_root() / "given"
        own.mkdir()
        given.mkdir()
        with self.drive(cwd=str(own)) as d:
            argv = self.echo(d, "cwd")
            for cwd, where in (("", own), (str(given), given)):
                with self.subTest(cwd=cwd):
                    ran = d.call("run", argv=argv, cwd=cwd)
                    (line,) = bytes.fromhex(ran["out"]).decode().splitlines()
                    self.assertTrue(os.path.samefile(line, where), (line, where))
                    child = d.call("start", argv=argv, cwd=cwd)["id"]
                    (line,) = self.lines(d, child, 1)
                    self.assertTrue(os.path.samefile(line, where), (line, where))
                    d.call("child_end", id=child)

    def test_environment_replaced(self):
        """A caller's pair of an allowlist name, ASCII letters folded,
        replaces v3's own entry of that name: the name once, the caller's
        value; the allowlist's other names v3's own holds kept, nothing
        else; on every adapter."""
        env = {**os.environ, "TEMP": os.environ.get("TEMP") or str(self.files_root())}
        pairs = [["TEMP", str(self.files_root() / "temp")], ["tmp", "a tmp"]]
        held = {name.casefold() for name in env}
        own = [n.casefold() for n in ALLOWLIST if n.casefold() in held]
        with self.drive(env=env) as d:
            argv = self.echo(d, "env", "TEMP", "TMP")
            ran = d.call("run", argv=argv, env=pairs)
        lines = bytes.fromhex(ran["out"]).decode().splitlines()
        self.assertEqual(sorted(n.casefold() for n in lines[:-2]), sorted({*own, "temp", "tmp"}), lines)
        self.assertEqual(lines[-2:], [f"TEMP={pairs[0][1]}", f"TMP={pairs[1][1]}"])

    def test_past_timeout(self):
        """read_line and wait past their timeout: deadline, the tree left
        running, each again; then the Child's end stops it. On the fake its
        hanging program; elsewhere qiven3-child's sleep, its process
        running after the timeouts and gone after the end."""
        with self.drive() as d:
            if self.ADAPTER == "fake":
                argv = programs(self, d, sleeper={"lines": ["pid 0"], "hang": True})["sleeper"]
            else:
                self.child_half("a child past read_line's and wait's timeouts")
                argv = self.child("sleep", LONG)
            child = d.call("start", argv=argv, deadline=LONG)["id"]
            (line,) = self.lines(d, child, 1)
            pid = int(line.split()[1])
            for op in ("child_read_line", "child_wait") * 2:
                with self.subTest(op=op):
                    error = self.failure(d.call, op, id=child, timeout=SHORT)
                    self.assert_platform_error(error, "deadline")
            if self.ADAPTER != "fake":
                ran = d.call("run", argv=self.child("gone", pid, 0))
                self.assertEqual(bytes.fromhex(ran["out"]), b"running\n", pid)
            d.call("child_end", id=child)
            if self.ADAPTER != "fake":
                self.assert_gone(d, pid)

    def test_child_memory(self):
        """Each Child request within the member's budget, nothing left on its
        arena; under QIVEN3_FAULTS, start's and read_line's arena
        allocations and the adapter's calls failed in turn, each a typed
        error, then done, the calls counted: on the fake over its programs,
        elsewhere over qiven3-child."""
        if self.ADAPTER != "fake":
            self.needs("processes")
            self.child_half("the Child requests' budgets and faults on a real child")
        with self.drive() as d:
            if self.ADAPTER == "fake":
                line = "x" * 200
                argv = programs(self, d, talker={"lines": [line] * 400})["talker"]
            else:
                line = "x" * 63
                argv = self.child("flood", "out", 400 * 64)
            self.budgets(d, argv, (line + "\n").encode().hex())

    def budgets(self, d, argv, first):
        """The Child requests' budgets on argv's child, then its faults;
        first, its first line in hexadecimal."""
        child = d.call("start", argv=argv)["id"]
        requests = (("start", {"argv": argv}), ("child_read_line", {"id": child}),
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
                if op == "start":
                    started = reply["result"]["id"]
        d.call("child_end", id=started)
        if not has_faults() or debug_library():
            return
        start_calls, read_line_calls = CALLS[self.ADAPTER]
        child = d.call("start", argv=argv)["id"]
        made = [child]
        for op, of, args, calls in (("start", "allocation", {"argv": argv}, 0),
                                    ("start", "platform", {"argv": argv}, start_calls),
                                    ("child_read_line", "allocation", {"id": child}, None),
                                    ("child_read_line", "platform", {"id": child}, read_line_calls)):
            with self.subTest(op=op, of=of):
                k, reply = d.sweep(op, of, **args)
                if op == "start":
                    made.append(reply["result"]["id"])
                self.assertEqual(reply["fault"]["calls"], k - 1, reply)
                # The line's allocations follow the standard library's growth
                # (one on MSVC's, two on libstdc++); the fake's calls do not.
                if calls is None:
                    self.assertGreaterEqual(k - 1, 1, reply)
                else:
                    self.assertEqual(k - 1, calls, reply)
        self.assertEqual(reply["result"], first)
        for each in made:
            d.call("child_end", id=each)
