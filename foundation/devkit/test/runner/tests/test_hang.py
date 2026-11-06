"""The watcher's hang path (watch.py, foundation/devkit/python/debugger.py): K8, the
plant `hang` of qiven_diag_fault, its main thread and one more each blocked
for ever, under a limit of 3 s, ends with the watcher's hang line, every
thread's stack, both threads' holding plant_hang (in a build with no PDB
beside its binaries, both threads' sleep called from the binary's own
code, which names no function), its dump, status 124,
no window, within the limit plus 30 s and 200 lines; a case's process
tree killed with it; and with no debugger at hand, "hang: no debugger, no
stacks", still killed, 124. The status line names what the watcher wrote:
the stacks of the threads and the dump, or "no debugger, no stack
written".

The debugger is cdb.exe, pinned in foundation/devkit/python/debugger_pins.txt;
Linux's, gdb, with tools/linux-pins.txt, is retired (tools/README.md's
Retired section).
In a CI job on Windows
(GITHUB_ACTIONS "true") the watcher takes the image's cdb, here a stand-in
cdb.cmd on PATH, its -version, one thread's stack and the dump the watcher asks:
its stacks and dump written, the status line carrying the debugger's how as
the debugger gives it; with none, its reason so. The words of how and of the
reason, and off CI never a cdb the pins refuse, are debugger's own, proven
there. Where the pinned cdb is absent, K8 skips by
name, "no cdb", unittest's skip, which CTest reads as skipped; one off its pin fails. Under a sanitizer, whose runtime handles the process's
faults and threads, K8 is skipped as the planted faults are.

K8 drives the debugger: the label debugger, never fake, so CI never runs
it. The runner (tools/changed.py) runs it under CTest by its module's
label, runner, and the presets full and debugger hold it. It needs what is
said above: a debugger at hand and, on Windows, for plant_hang's name, a
PDB beside the binaries. The watcher's other ends here are fake's.
"""

import os
import re
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from foundation.devkit.python import debugger
from foundation.devkit.python import status as status_table
from foundation.devkit.python import system
from foundation.devkit.python.answer import LINE_POINTS
from foundation.devkit.test.runner import binaries
from foundation.devkit.test.runner.binaries import AFTER, POLL, run_watched, sanitized

WINDOWS = system.WINDOWS
FAULT = "qiven_diag_fault"
LIMIT = 3
HANG = re.compile(r"^hang: past its limit of 3 s, (?P<threads>\d+) threads$")
THREAD = debugger.CDB_THREAD
# A frame of plant_hang: its own, or its lambda's, which its name qualifies.
IN_PLANT = re.compile(r"[!\s`]plant_hang(?:[+'(\s]|$)")
# A stand-in for a CI image's cdb: its -version, and for a hang one
# thread's stack as ~*kn gives it and the dump .dump /mt names.
STAND_IN = """import re, sys
args = sys.argv[1:]
if args == ["-version"]:
    print("cdb version 10.0.99999.1")
    sys.exit(0)
commands = args[args.index("-c") + 1]
print(f"0:000> cdb: Reading initial command '{commands}'")
print("   0  Id: 1a2b.1c Suspend: 1 Teb: 00000000`00000000 Unfrozen")
print("00 00000000`00000000 00000000`00000000 standin!wait+0x10")
dump = re.search(r"\\.dump /mt (.+?); qd", commands)
if dump:
    open(dump.group(1), "wb").close()
"""


def stand_in(root):
    """A stand-in cdb (cdb.cmd running STAND_IN) in root/bin: its path."""
    (root / "bin").mkdir()
    script = root / "cdb_stand_in.py"
    script.write_text(STAND_IN, encoding="utf-8")
    cmd = root / "bin" / "cdb.cmd"
    cmd.write_text(f'@"{sys.executable}" -I "{script}" %*\n', encoding="ascii")
    return cmd


def debugger_reason():
    """Why no debugger is at hand, or None; one off its pin raises."""
    try:
        debugger.find()
    except debugger.Absent as e:
        return str(e)
    return None


def lines_of(data):
    """A watched run's output, as lines."""
    return data.decode("utf-8", "replace").splitlines()


def sections(lines):
    """The stacks' lines, by thread."""
    out = []
    for line in lines:
        if THREAD.match(line):
            out.append([line])
        elif out:
            out[-1].append(line)
    return out


def ci_environment(root, **changes):
    """This environment as a CI job's (GITHUB_ACTIONS "true"), the build
    root/build (made), the names changed without regard to case."""
    (root / "build").mkdir()
    changes = {debugger.CI_VARIABLE: "true", "QIVEN_BUILD": str(root / "build"), **changes}
    names = {name.upper() for name in changes}
    return {**{k: v for k, v in os.environ.items() if k.upper() not in names}, **changes}


def given(env):
    """What the hang's debugger gives in the environment env: its how, or
    the reason it has none. Its words are debugger's own to prove."""
    with mock.patch.dict(os.environ, env, clear=True):
        try:
            return debugger.hang_debugger()[1]
        except debugger.NotFound as e:
            return str(e)


class HangStacks(unittest.TestCase):
    ctest_labels = ("debugger",)

    def test_K8_hang(self):
        if sanitized():
            self.skipTest("under a sanitizer its report stands for the entry's")
        try:
            reason = debugger_reason()
        except debugger.NotFound as e:
            self.fail(f"the debugger: {e}")
        if reason is not None:
            self.skipTest(reason)
        dumps = binaries.directory("QIVEN_BUILD") / "dumps"
        start = time.time()
        status, _, err, seconds, seen = run_watched([binaries.path(FAULT), "hang"], "K8", limit=LIMIT)
        err = lines_of(err)
        text = "\n".join(err)
        self.assertEqual(status, 124, text)
        self.assertEqual(seen, [], "K8: a window opened")
        self.assertLessEqual(seconds, LIMIT + AFTER, text)
        at = next((i for i, line in enumerate(err) if HANG.match(line)), None)
        self.assertIsNotNone(at, text)
        hang = err[at:-1]
        self.assertLessEqual(len(hang), debugger.HANG_LINES, text)
        self.assertTrue(all(len(line) <= LINE_POINTS for line in hang), text)
        stacks = sections(hang[1:])
        threads = int(HANG.match(hang[0])["threads"])
        self.assertEqual(len(stacks), min(threads, debugger.HANG_THREADS), text)
        if not binaries.path(FAULT).with_suffix(".pdb").is_file():
            # A build that keeps no PDB beside its binaries (Release, whose
            # binaries the clean-binaries check reads) names no function:
            # both threads by their sleep called from the binary's own code.
            own = re.compile(rf"\s{re.escape(debugger.module(binaries.path(FAULT)))}\+0x")
            held = [s for s in stacks if any("!SleepEx+" in line for line in s)
                    and any(own.search(line) for line in s)]
        else:
            held = [s for s in stacks if any(IN_PLANT.search(line) for line in s)]
        self.assertGreaterEqual(len(held), 2, f"K8: plant_hang in {len(held)} threads' stacks: {text}")
        # The reading's control: plant_null, on no stack, is found in none.
        self.assertFalse(any(re.search(r"[!\s]plant_null[+(\s]", line) for line in hang), text)
        # Named in the hang's lines (cdb's "Creating <file>"): another
        # case's hang, run beside this one, writes its own.
        written = [p for p in dumps.glob(f"{FAULT}-*-hang.dmp") if p.stat().st_mtime >= start - 1
                   and str(p.name) in text]
        self.assertEqual(len(written), 1, f"K8: the dumps {written}")
        self.assertEqual(written[0].read_bytes()[:4], b"MDMP")
        # The status names what the watcher wrote: the stacks of the threads, and the dump.
        self.assertEqual(err[-1], status_table.line(124) + f"; {len(stacks)} of {threads} threads' stacks written, "
                         f"its dump {written[0].name}; K8 past {LIMIT} s", text)


class Hang(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_tree_killed(self):
        """A case past its limit is killed with the process it started."""
        code = (f"import sys, time; sys.path.append({str(binaries.ROOT)!r}); "
                "from foundation.devkit.python import process; "
                "child = process.start([sys.executable, '-I', '-c', 'import time; time.sleep(120)'], "
                "out=process.INHERIT, err=process.INHERIT); "
                "print(child.pid, flush=True); time.sleep(120)")
        status, out, err, seconds, _ = run_watched([sys.executable, "-I", "-c", code], "hang.tree", limit=2)
        out, err = lines_of(out), lines_of(err)
        self.assertEqual(status, 124, err)
        self.assertLessEqual(seconds, 2 + AFTER, err)
        child = int(out[0])
        deadline = time.monotonic() + 10
        while system.alive(child) and time.monotonic() < deadline:
            time.sleep(POLL)
        self.assertFalse(system.alive(child), f"the case's child {child} outlived it")

    def test_ci_image_cdb(self):
        """In a CI job on Windows, with the image's cdb (here the stand-in on
        PATH): its stacks and dump written, killed, 124, the status carrying
        the debugger's how as it gives it."""
        if not WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            standin = stand_in(Path(root))
            env = ci_environment(Path(root), PATH=str(standin.parent))
            status, _, err, seconds, _ = run_watched([sys.executable, "-I", "-c", "import time; time.sleep(60)"],
                                                     "hang.ci", env, limit=1)
            err = lines_of(err)
            self.assertEqual(status, 124, err)
            self.assertIn("00 00000000`00000000 00000000`00000000 standin!wait+0x10", err)
            self.assertRegex(err[-1], "^" + re.escape(status_table.line(124)) + r"; 1 of 1 threads' stacks written, "
                             r"its dump \S+-\d+-hang\.dmp, " + re.escape(given(env)) + r"; hang\.ci past 1 s$")
            self.assertEqual(len(list((Path(root) / "build" / "dumps").glob("*-hang.dmp"))), 1, err)
            self.assertLessEqual(seconds, 1 + AFTER, err)

    def test_ci_no_cdb(self):
        """In a CI job on Windows with no cdb on PATH nor in the SDK's
        Debuggers/x64: no stacks, killed, 124, the status carrying the
        debugger's reason as it gives it."""
        if not WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            empty, sdk = Path(root) / "empty", Path(root) / "sdk"
            empty.mkdir()
            sdk.mkdir()
            env = ci_environment(Path(root), PATH=str(empty), WindowsSdkDir=str(sdk))
            status, _, err, seconds, _ = run_watched([sys.executable, "-I", "-c", "import time; time.sleep(60)"],
                                                     "hang.ci_none", env, limit=1)
            err = lines_of(err)
            why = given(env)
            self.assertEqual(status, 124, err)
            self.assertEqual(err[-3:-1], ["hang: no debugger, no stacks", f"hang: {why}"], err)
            self.assertEqual(err[-1], status_table.line(124) + f"; no debugger, no stack written: {why}; "
                             "hang.ci_none past 1 s", err)
            self.assertLessEqual(seconds, 1 + AFTER, err)


if __name__ == "__main__":
    unittest.main()
