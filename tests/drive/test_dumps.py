"""The dumps' bound (tools/dumps.py), run by the watcher after each case:
a dumps' directory keeps at most DUMPS_KEPT dumps (*.dmp, *.core), the
oldest removed first and each named in one line, never the case's own nor
the newest, nothing else in the directory touched.

Its logic on fake dumps in a scratch directory; through the watcher with
a scratch build whose dumps/ holds fake dumps, the case writing one of its
own; and, with the debugger at hand, through K8's real hang, whose dump
the watcher writes. The fake cases carry the label fake; the hang's,
debugger, and skip as K8 does.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.test_faults import FAULT, sanitized
from tests.drive.test_hang import AFTER, LIMIT, debugger_reason
from tools import debuggers
from tools import dumps as bound_of
from tools import status as status_table

WINDOWS = sys.platform == "win32"
KEPT = bound_of.DUMPS_KEPT
BASE = 1_600_000_000  # the fake dumps' last writes, in seconds, oldest first


def fake(directory, count, suffixes=(".dmp", ".core")):
    """count fake dumps in directory, the i-th written at BASE + i with
    i + 1 bytes: their names, oldest first."""
    names = []
    for i in range(count):
        name = f"fake{i:02d}-{100000 + i}{'-hang' if i % 3 == 0 else ''}{suffixes[i % len(suffixes)]}"
        path = Path(directory) / name
        path.write_bytes(b"x" * (i + 1))
        os.utime(path, ns=((BASE + i) * 10**9, (BASE + i) * 10**9))
        names.append(name)
    return names


def held(directory):
    """The directory's dumps: {name: size}."""
    return {p.name: p.stat().st_size for p in Path(directory).iterdir() if p.suffix in bound_of.SUFFIXES}


def watched(command, build, limit):
    """Runs command behind the watcher with QIVEN3_BUILD the scratch build:
    its status and standard error's lines, unfiltered."""
    env = dict(os.environ, QIVEN3_BUILD=str(build))
    done = subprocess.run(binaries.watched(command, "dumps.case", limit), capture_output=True, env=env,
                          stdin=subprocess.DEVNULL, timeout=limit + AFTER + 60, check=False)
    return done.returncode, done.stderr.decode("utf-8", "replace").splitlines()


def removed(name):
    return f"dumps: removed {name}, past the newest {KEPT}"


class Bound(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_bound(self):
        """Past the bound, the oldest go first, the pid's own stays, the
        rest of the directory is untouched; a second pass removes none."""
        self.assertGreaterEqual(KEPT, 1)
        with tempfile.TemporaryDirectory() as scratch:
            names = fake(scratch, KEPT + 5)
            mine = "mine-4242-hang.core"
            (Path(scratch) / mine).write_bytes(b"own")
            os.utime(Path(scratch) / mine, ns=((BASE - 10) * 10**9,) * 2)
            other = Path(scratch) / "notes.txt"
            other.write_bytes(b"no dump")
            os.utime(other, ns=((BASE - 20) * 10**9,) * 2)
            lines = bound_of.bound(scratch, 4242)
            # Six past the bound: the pid's own, the oldest, kept; five removed.
            self.assertEqual(lines, [removed(n) for n in names[:5]])
            self.assertEqual(sorted(held(scratch)), sorted([mine, *names[5:]]))
            self.assertTrue(other.is_file())
            self.assertEqual(bound_of.bound(scratch, 4242), [])
            # Without the pid, its dump is the oldest past the bound.
            self.assertEqual(bound_of.bound(scratch), [removed(mine)])
            self.assertEqual(sorted(held(scratch)), names[5:])
            self.assertEqual(sum(held(scratch).values()), sum(range(6, KEPT + 6)))

    def test_within(self):
        """At the bound or under it, or with no directory, none removed."""
        with tempfile.TemporaryDirectory() as scratch:
            names = fake(scratch, KEPT)
            self.assertEqual(bound_of.bound(scratch), [])
            self.assertEqual(sorted(held(scratch)), names)
            self.assertEqual(bound_of.bound(Path(scratch) / "none"), [])

    def test_watcher(self):
        """The case writes a dump into a dumps/ at the bound: the oldest is
        removed and named before the status line; the case's own and the
        newest kept, DUMPS_KEPT in all."""
        with tempfile.TemporaryDirectory() as build:
            dumps = Path(build) / "dumps"
            dumps.mkdir()
            names = fake(dumps, KEPT)
            code = ("import os; from pathlib import Path; "
                    "(Path(os.environ['QIVEN3_DUMP_DIR']) / f'python-{os.getpid()}.dmp').write_bytes(b'MDMP')")
            status, err = watched([sys.executable, "-I", "-c", code], build, 60)
            self.assertEqual(status, 0, err)
            self.assertEqual(err, [removed(names[0]), status_table.line(0)])
            kept = held(dumps)
            self.assertEqual(len(kept), KEPT, kept)
            self.assertEqual(sum(kept.values()), sum(range(2, KEPT + 1)) + 4, kept)
            self.assertTrue(any(n.startswith("python-") and n.endswith(".dmp") for n in kept), kept)

    def test_watcher_own(self):
        """The case's own dump, however old, is never removed: the oldest
        other is."""
        with tempfile.TemporaryDirectory() as build:
            dumps = Path(build) / "dumps"
            dumps.mkdir()
            names = fake(dumps, KEPT + 1)
            code = ("import os; from pathlib import Path; "
                    "p = Path(os.environ['QIVEN3_DUMP_DIR']) / f'python-{os.getpid()}-hang.core'; "
                    f"p.write_bytes(b'own'); os.utime(p, ns=({BASE - 10} * 10**9,) * 2)")
            status, err = watched([sys.executable, "-I", "-c", code], build, 60)
            self.assertEqual(status, 0, err)
            self.assertEqual(err, [removed(names[0]), status_table.line(0)])
            kept = held(dumps)
            self.assertEqual(len(kept), KEPT + 1, kept)
            self.assertEqual(sorted(n for n in kept if n.startswith("fake")), names[1:])


class HangBound(unittest.TestCase):
    ctest_labels = ("debugger",)

    def test_hang(self):
        """K8's hang behind the watcher, its dumps/ at the bound with fake
        dumps: the real hang dump written and kept, the oldest fake removed
        and named, DUMPS_KEPT in all."""
        if sanitized():
            self.skipTest("under a sanitizer its report stands for the entry's")
        try:
            reason = debugger_reason()
        except debuggers.NotFound as e:
            self.fail(f"the debugger: {e}")
        if reason is not None:
            self.skipTest(reason)
        with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as build:
            dumps = Path(build) / "dumps"
            dumps.mkdir()
            names = fake(dumps, KEPT)
            status, err = watched([binaries.path(FAULT), "hang"], build, LIMIT)
            text = "\n".join(err)
            self.assertEqual(status, 124, text)
            self.assertEqual(err[-2], removed(names[0]), text)
            self.assertRegex(err[-1], "^" + re.escape(status_table.line(124)) + r"; \d+ of \d+ threads' stacks written, "
                             rf"its dump {re.escape(FAULT)}-\d+-hang\.(?:dmp|core); dumps\.case past {LIMIT} s$")
            kept = held(dumps)
            self.assertEqual(len(kept), KEPT, kept)
            suffix = "-hang." + ("dmp" if WINDOWS else "core")
            real = [n for n in kept if n.startswith(f"{FAULT}-") and n.endswith(suffix)]
            self.assertEqual(len(real), 1, kept)
            self.assertIn(real[0], text)
            self.assertEqual((dumps / real[0]).read_bytes()[:4], b"MDMP" if WINDOWS else b"\x7fELF")
            self.assertEqual(sum(kept.values()), sum(range(2, KEPT + 1)) + kept[real[0]], kept)
