"""The watcher's hang path (tools/watch.py, tools/debuggers.py): K8, the
plant `hang` of qiven3-fault, its main thread and one more each blocked
for ever, under a limit of 3 s, ends with the watcher's hang line, every
thread's stack, both threads' holding plant_hang (in a build with no PDB
beside its binaries, both threads' sleep called from the binary's own
code, which names no function), its dump, status 124,
no window, within the limit plus 30 s and 200 lines; a case's process
tree killed with it; and with no debugger at hand, "hang: no debugger, no
stacks", still killed, 124. The status line names what the watcher wrote:
the stacks of the threads and the dump, or "no debugger, no stack
written".

On Windows the debugger is cdb.exe, pinned in tools/windows-pins.txt; on
Linux gdb, pinned in tools/linux-pins.txt. In a CI job on Windows
(GITHUB_ACTIONS "true") the watcher takes the image's cdb, unpinned, its
version and path in the status line, or says why it has none and where it
looked; off CI never a cdb the pins refuse (a stand-in on PATH, for both). Where it is absent, K8 skips by
name, "no cdb" or "no gdb", with the runner's 125; one off its pin fails. Under a sanitizer, whose runtime handles the process's
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
from unittest import mock

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
    """Why no debugger is at hand, or None; one off its pin raises."""
    try:
        debuggers.find()
    except debuggers.Absent as e:
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
                binaries.unbounded(err.read()).decode("utf-8", "replace").splitlines(), time.monotonic() - start,
                seen)


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


def ci_environment(root, **changes):
    """This environment as a CI job's (GITHUB_ACTIONS "true"), the build
    root/build (made), the names changed without regard to case."""
    (root / "build").mkdir()
    changes ={debuggers.CI_VARIABLE: "true", "QIVEN3_BUILD": str(root / "build"), **changes}
    names = {name.upper() for name in changes}
    return {**{k: v for k, v in os.environ.items() if k.upper() not in names}, **changes}


class HangStacks(unittest.TestCase):
    ctest_labels = ("debugger",)

    def test_K8_hang(self):
        if sanitized():
            self.skipTest("under a sanitizer its report stands for the entry's")
        try:
            reason = debugger_reason()
        except debuggers.NotFound as e:
            self.fail(f"the debugger: {e}")
        if reason is not None:
            self.skipTest(reason)
        dumps = binaries.directory("QIVEN3_BUILD") / "dumps"
        start = time.time()
        status, _, err, seconds, seen = run_watched([binaries.path(FAULT), "hang"], "K8", LIMIT)
        text = "\n".join(err)
        self.assertEqual(status, 124, text)
        self.assertEqual(seen, [], "K8: a window opened")
        self.assertLessEqual(seconds, LIMIT + AFTER, text)
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
        # Named in the hang's lines (cdb's "Creating <file>", gdb's "Saved corefile <file>"): another
        # case's hang, run beside this one, writes its own.
        written = [p for p in dumps.glob(f"{FAULT}-*-hang.{suffix}") if p.stat().st_mtime >= start - 1
                   and str(p.name) in text]
        self.assertEqual(len(written), 1, f"K8: the dumps {written}")
        magic = b"MDMP" if WINDOWS else b"\x7fELF"
        self.assertEqual(written[0].read_bytes()[:4], magic)
        # The status names what the watcher wrote: the stacks of the threads, and the dump.
        self.assertEqual(err[-1], status_table.line(124) + f"; {len(stacks)} of {threads} threads' stacks written, "
                         f"its dump {written[0].name}; K8 past {LIMIT} s", text)


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
        self.assertEqual(err[-1], status_table.line(124) + f"; no debugger, no stack written: {debuggers.NO_GDB}; "
                         "hang.none past 1 s", err)
        self.assertLessEqual(seconds, 1 + AFTER, err)

    def test_pinned_here(self):
        """Off CI (no GITHUB_ACTIONS), the hang's debugger is the pinned
        one, or none with the pin's reason: never a cdb on PATH, which the
        pins refuse."""
        if not WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            standin = stand_in(Path(root))
            with mock.patch.dict(os.environ, {"PATH": f"{standin.parent}{os.pathsep}{os.environ.get('PATH', '')}"}):
                os.environ.pop(debuggers.CI_VARIABLE, None)
                try:
                    path, how = debuggers.hang_debugger()
                except debuggers.NotFound as e:
                    path, how = None, str(e)
            self.assertNotIn(str(standin.parent), str(path), how)
            self.assertNotIn(standin.parent.as_posix(), str(how))
            if path is not None:
                self.assertIsNone(how)

    def test_ci_image_cdb(self):
        """In a CI job on Windows the hang's debugger is the image's cdb,
        unpinned (here a stand-in on PATH): its stacks and dump written and
        the status naming its version and path."""
        if not WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            standin = stand_in(Path(root))
            env = ci_environment(Path(root), PATH=str(standin.parent))
            status, _, err, seconds, _ = run_watched([sys.executable, "-I", "-c", "import time; time.sleep(60)"],
                                                     "hang.ci", 1, env)
            self.assertEqual(status, 124, err)
            self.assertIn("00 00000000`00000000 00000000`00000000 standin!wait+0x10", err)
            self.assertRegex(err[-1], "^" + re.escape(status_table.line(124)) + r"; 1 of 1 threads' stacks written, "
                             r"its dump \S+-\d+-hang\.dmp, by the image's cdb 10\.0\.99999\.1 at "
                             + re.escape(standin.parent.as_posix()) + r"/cdb\.\w+, unpinned \(CI\); hang\.ci past 1 s$")
            self.assertEqual(len(list((Path(root) / "build" / "dumps").glob("*-hang.dmp"))), 1, err)
            self.assertLessEqual(seconds, 1 + AFTER, err)

    def test_ci_no_cdb(self):
        """In a CI job on Windows with no cdb on PATH nor in the SDK's
        Debuggers/x64: no stacks, killed, 124, the status saying why and
        where the watcher looked."""
        if not WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            empty, sdk = Path(root) / "empty", Path(root) / "sdk"
            empty.mkdir()
            sdk.mkdir()
            env = ci_environment(Path(root), PATH=str(empty), WindowsSdkDir=str(sdk))
            status, _, err, seconds, _ = run_watched([sys.executable, "-I", "-c", "import time; time.sleep(60)"],
                                                     "hang.ci_none", 1, env)
            why = (f"no cdb: none on PATH, none at {(sdk / 'Debuggers' / 'x64' / 'cdb.exe').as_posix()}, "
                   "the SDK's root by WindowsSdkDir")
            self.assertEqual(status, 124, err)
            self.assertEqual(err[-3:-1], ["hang: no debugger, no stacks", f"hang: {why}"], err)
            self.assertEqual(err[-1], status_table.line(124) + f"; no debugger, no stack written: {why}; "
                             "hang.ci_none past 1 s", err)
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
