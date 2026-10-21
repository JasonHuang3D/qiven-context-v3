"""The watcher's hang path (tools/watch.py, tools/debuggers.py): K8, the
plant `hang` of qiven3-fault, its main thread and one more each blocked
for ever, under a limit of 3 s, ends with the watcher's hang line, every
thread's stack, both threads' holding plant_hang (in a build with no PDB
beside its binaries, both threads' sleep called from the binary's own
code, which names no function), its dump, status 124,
no window, within the limit plus 30 s and 200 lines; a case's process
tree killed with it; and with no debugger at hand, "hang: no debugger, no
stacks", still killed, 124.

On Windows the debugger is cdb.exe, pinned in tools/windows-pins.txt; on
Linux gdb, which is not installed: the cases that need it are skipped
with the reason. Under a sanitizer, whose runtime handles the process's
faults and threads, K8 is skipped as the planted faults are.

K8 drives the debugger: the label debugger, never fake, so the loop runs
it and CI never does; the watcher's other ends here are fake's.
"""

import os
import re
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.test_faults import FAULT, LINE_POINTS, POLL, sanitized, windows_of
from tools import debuggers
from tools import status as status_table

WINDOWS = sys.platform == "win32"
LIMIT = 3
AFTER = 30.0  # the watcher's seconds past the limit at most
HANG_LINES = 200
HANG = re.compile(r"^hang: past its limit of 3 s, (?P<threads>\d+) threads$")
THREAD = debuggers.CDB_THREAD if WINDOWS else debuggers.GDB_THREAD
# A frame of plant_hang: its own, or its lambda's, which its name qualifies.
IN_PLANT = re.compile(r"[!\s`]plant_hang(?:[+'(\s]|$)")


def debugger_reason():
    """Why no debugger is at hand, or None."""
    try:
        debuggers.find()
    except debuggers.NotFound as e:
        return str(e)
    return None


def run_watched(command, name, limit, env=None):
    """Runs a command behind the watcher: its status, out, err lines,
    seconds and the windows seen."""
    start = time.monotonic()
    with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        process = subprocess.Popen(binaries.watched(command, name, limit), stdin=subprocess.DEVNULL, stdout=out,
                                   stderr=err, env=env)
        seen = []
        while process.poll() is None:
            if WINDOWS:
                seen += windows_of(process.pid)
            if time.monotonic() - start > limit + AFTER + 30:
                process.kill()
                break
            time.sleep(POLL)
        status = process.wait()
        out.seek(0)
        err.seek(0)
        return (status, out.read().decode("utf-8", "replace").splitlines(),
                err.read().decode("utf-8", "replace").splitlines(), time.monotonic() - start, seen)


def sections(lines):
    """The stacks' lines, by thread."""
    out = []
    for line in lines:
        if THREAD.match(line):
            out.append([line])
        elif out:
            out[-1].append(line)
    return out


def alive(pid):
    """Whether the process pid still runs."""
    if WINDOWS:
        done = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, timeout=30,
                              check=False)
        return str(pid) in done.stdout.decode("utf-8", "replace")
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return not Path(f"/proc/{pid}/stat").read_text().split(")")[-1].split()[0] == "Z"


class HangStacks(unittest.TestCase):
    ctest_labels = ("debugger",)

    def test_K8_hang(self):
        if sanitized():
            self.skipTest("under a sanitizer its report stands for the entry's")
        reason = debugger_reason()
        if reason is not None and not WINDOWS:
            self.skipTest(reason)
        self.assertIsNone(reason, "K8 needs the debugger")
        dumps = binaries.directory("QIVEN3_BUILD") / "dumps"
        start = time.time()
        status, _, err, seconds, seen = run_watched([binaries.path(FAULT), "hang"], "K8", LIMIT)
        text = "\n".join(err)
        self.assertEqual(status, 124, text)
        self.assertEqual(seen, [], "K8: a window opened")
        self.assertLessEqual(seconds, LIMIT + AFTER, text)
        self.assertEqual(err[-1], status_table.line(124) + f"; K8 past {LIMIT} s", text)
        at = next((i for i, line in enumerate(err) if HANG.match(line)), None)
        self.assertIsNotNone(at, text)
        hang = err[at:-1]
        self.assertLessEqual(len(hang), HANG_LINES, text)
        self.assertTrue(all(len(line) <= LINE_POINTS for line in hang), text)
        stacks = sections(hang[1:])
        threads = int(HANG.match(hang[0])["threads"])
        self.assertEqual(len(stacks), min(threads, debuggers.HANG_THREADS), text)
        if WINDOWS and not binaries.path(FAULT).with_suffix(".pdb").is_file():
            # A build that keeps no PDB beside its binaries (Release, whose
            # binaries the clean-binaries check reads) names no function:
            # both threads by their sleep called from the binary's own code.
            own = re.compile(rf"\s{re.escape(debuggers.module(binaries.path(FAULT)))}\+0x")
            held = [s for s in stacks if any("!SleepEx+" in line for line in s)
                    and any(own.search(line) for line in s)]
        else:
            held = [s for s in stacks if any(IN_PLANT.search(line) for line in s)]
        self.assertGreaterEqual(len(held), 2, f"K8: plant_hang in {len(held)} threads' stacks: {text}")
        # The reading's control: plant_null, on no stack, is found in none.
        self.assertFalse(any(re.search(r"[!\s]plant_null[+(\s]", line) for line in hang), text)
        suffix = "dmp" if WINDOWS else "core"
        written = [p for p in dumps.glob(f"{FAULT}-*-hang.{suffix}") if p.stat().st_mtime >= start - 1]
        self.assertEqual(len(written), 1, f"K8: the dumps {written}")
        magic = b"MDMP" if WINDOWS else b"\x7fELF"
        self.assertEqual(written[0].read_bytes()[:4], magic)


class Hang(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_tree_killed(self):
        """A case past its limit is killed with the process it started."""
        code = ("import subprocess, sys, time; "
                "child = subprocess.Popen([sys.executable, '-I', '-c', 'import time; time.sleep(120)']); "
                "print(child.pid, flush=True); time.sleep(120)")
        status, out, err, seconds, _ = run_watched([sys.executable, "-I", "-c", code], "hang.tree", 2)
        self.assertEqual(status, 124, err)
        self.assertLessEqual(seconds, 2 + AFTER, err)
        child = int(out[0])
        deadline = time.monotonic() + 10
        while alive(child) and time.monotonic() < deadline:
            time.sleep(POLL)
        self.assertFalse(alive(child), f"the case's child {child} outlived it")

    def test_no_debugger(self):
        """With no debugger at hand the watcher says so, kills, and exits
        124: on Linux, where gdb is found on PATH, a PATH without it."""
        if WINDOWS:
            self.skipTest("cdb is found by the registry, not by PATH")
        env = dict(os.environ, PATH="")
        status, _, err, seconds, _ = run_watched([sys.executable, "-I", "-c", "import time; time.sleep(60)"],
                                                 "hang.none", 1, env)
        self.assertEqual(status, 124, err)
        self.assertEqual(err[-3:-1], ["hang: no debugger, no stacks", f"hang: {debuggers.NO_GDB}"], err)
        self.assertLessEqual(seconds, 1 + AFTER, err)

    def test_hang_lines_bound(self):
        """The stacks' reading keeps 16 threads and 199 lines, with [cut]."""
        if WINDOWS:
            header = "   {i}  Id: 1a2b.{i:x} Suspend: 1 Teb: 00000000`00000000 Unfrozen"
            text = "0:000> cdb: Reading initial command '~*kn 32; qd'\n"
        else:
            header = 'Thread {i} (Thread 0x7f00 (LWP {i}) "qiven3-fault"):'
            text = ""
        text += "".join(header.format(i=i + 1) + "\n" + "#0 frame\n" * 3 for i in range(20))
        threads, lines = debuggers.hang_lines(text)
        self.assertEqual(threads, 20)
        self.assertEqual(len(sections(lines)), debuggers.HANG_THREADS)
        self.assertTrue(lines[-1].startswith(debuggers.CUT), lines[-1])
        threads, lines = debuggers.hang_lines(text.replace("#0 frame\n", "#0 frame\n" * 20))
        self.assertEqual(len(lines), HANG_LINES - 1)
        self.assertTrue(lines[-1].startswith(debuggers.CUT), lines[-1])
        self.assertEqual(debuggers.clip("x" * 400), "x" * (LINE_POINTS - 5) + debuggers.CUT)


if __name__ == "__main__":
    unittest.main()
