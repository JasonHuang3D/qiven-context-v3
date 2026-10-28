"""The binary's command line on Windows: wmain takes the arguments as
UTF-16 and the Windows adapter converts each after the program's name,
which it does not read, strictly to UTF-8 (WindowsArguments), through
qiven3-drive-windows' operation arguments and through the qiven3 binary
itself, P16's directory, cafe with U+00EB, among them.

The conversion's budget is its measure, argc + 1 pointers, the name's
null and each other argument's UTF-8 and its null, which its arena's peak
meets; a command line holds at most 32,767 units after the name and 16,384
arguments; an unpaired surrogate after the name is PlatformError path,
naming the argument; under QIVEN3_FAULTS each of its two allocations fails
in turn. The binary's answer it cannot write ends it with status 2.

On every system, the binary's per-call wire: a request past each bound or
not UTF-8 ends it with status 2 and no answer, one within them reaches its
command.
"""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.session import WINDOWS, Drive, DriveError, has_faults

POINTER = 8
UNITS = 32767
MOST = 16384
# P16's directory.
P16 = "caf\u00eb"


def units(text):
    """A string's UTF-16 units, a lone surrogate kept."""
    data = text.encode("utf-16-le", "surrogatepass")
    return [int.from_bytes(data[i:i + 2], "little") for i in range(0, len(data), 2)]


def measure(texts):
    """The conversion's bytes: the pointers, then the name's null, the
    other arguments' text and their nulls."""
    return (len(texts) + 1) * POINTER + sum(len(t.encode("utf-8")) + 1 for t in texts[1:]) + min(len(texts), 1)


def converted(texts):
    """The UTF-8 the conversion gives: the name empty, unread."""
    return [b""] * min(len(texts), 1) + [t.encode("utf-8") for t in texts[1:]]


def scratch(case):
    """A scratch directory of the case's own, removed after it."""
    made = tempfile.TemporaryDirectory(prefix="qiven3-arguments-")
    case.addCleanup(made.cleanup)
    return Path(made.name)


class WindowsOnly(unittest.TestCase):
    ctest_labels = ("fake",)

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the binary takes UTF-16 arguments on Windows alone")


class Conversion(WindowsOnly):
    TEXTS = ["qiven3", "location", P16, "", "a b\"c", "a\u00eb\u4e2d\u20ac\U0001f600\ue000"]

    def setUp(self):
        super().setUp()
        self.drive = Drive(drive=WINDOWS)
        self.addCleanup(self.drive.close)

    def convert(self, texts, **args):
        return self.drive.reply("arguments", units=[units(t) for t in texts], **args)

    def test_utf8(self):
        reply = self.convert(self.TEXTS)
        self.assertTrue(reply["ok"], reply)
        result = reply["result"]
        self.assertEqual([bytes.fromhex(a) for a in result["argv"]], converted(self.TEXTS))
        self.assertEqual(result["argc"], len(self.TEXTS))
        self.assertTrue(result["ended"])
        memory = reply["memory"]
        self.assertEqual(memory["budget"], measure(self.TEXTS))
        self.assertEqual(memory["peak"], memory["budget"])
        self.assertEqual(memory["left"], 0)

    def test_none(self):
        reply = self.convert([])
        self.assertTrue(reply["ok"], reply)
        self.assertEqual((reply["result"]["argc"], reply["result"]["ended"]), (0, True))
        self.assertEqual(reply["memory"]["peak"], POINTER)

    def test_unpaired(self):
        for lone in ("\ud800", "\udc00", "\ud83d"):
            with self.subTest(unit=hex(ord(lone))):
                reply = self.convert(["qiven3", "a" + lone + "b" if lone != "\ud83d" else "a" + lone])
                self.assertFalse(reply["ok"])
                error = reply["error"]
                self.assertEqual((error["kind"], error["code"], error["operation"]), ("platform", "path", "arguments"))
                self.assertIn("argument 1 ", error["detail"])
                self.assertIn("ERROR_NO_UNICODE_TRANSLATION (1113)", error["detail"])

    def test_name(self):
        """The name is not read: one not valid UTF-16 converts, empty."""
        texts = ["q" + chr(0xD800), "location"]
        reply = self.convert(texts)
        self.assertTrue(reply["ok"], reply)
        self.assertEqual([bytes.fromhex(a) for a in reply["result"]["argv"]], [b"", b"location"])
        self.assertEqual(reply["memory"]["peak"], measure(texts))

    def test_short(self):
        reply = self.convert(self.TEXTS, budget=measure(self.TEXTS) - 1)
        self.assertFalse(reply["ok"])
        self.assertEqual((reply["error"]["kind"], reply["error"]["code"]), ("memory", "budget"))
        self.assertEqual(reply["memory"]["left"], 0)

    def test_bound(self):
        widest = ["qiven3", "\u20ac" * UNITS]
        reply = self.convert(widest)
        self.assertTrue(reply["ok"], reply.get("error"))
        self.assertEqual(reply["memory"]["peak"], 3 * POINTER + 1 + 3 * UNITS + 1)
        reply = self.convert([""] * MOST)
        self.assertTrue(reply["ok"], reply.get("error"))
        self.assertEqual(reply["memory"]["peak"], (MOST + 1) * POINTER + MOST)
        for past in (["qiven3", "\u20ac" * UNITS, "a"], [""] * (MOST + 1)):
            with self.subTest(argc=len(past)):
                with self.assertRaises(DriveError) as caught:
                    self.drive.call("arguments", units=[units(t) for t in past])
                self.assertEqual((caught.exception.kind, caught.exception.code), ("platform", "memory"))

    def test_faults(self):
        if not has_faults():
            self.skipTest("not applicable: QIVEN3_FAULTS is off in this build")
        for texts, allocations in ((self.TEXTS, 2), ([], 1)):
            with self.subTest(argc=len(texts)):
                given = [units(t) for t in texts]
                k, reply = self.drive.sweep("arguments", "allocation", units=given)
                self.assertEqual(k, allocations + 1)
                self.assertTrue(reply["ok"], reply)
                self.assertEqual(reply["memory"]["left"], 0)
                # Each failed run gives back what it took.
                for k in range(1, allocations + 1):
                    failed = self.drive.reply("arguments", units=given, fault={"k": k, "of": "allocation"})
                    self.assertEqual((failed["ok"], failed["memory"]["left"]), (False, 0), failed)


class Binary(WindowsOnly):
    """qiven3 itself: location init with --config naming P16's directory
    makes it, by its name, before the command goes on."""

    def setUp(self):
        super().setUp()
        self.root = scratch(self)

    def run_binary(self, *args):
        return subprocess.run([str(binaries.path("qiven3")), *args], capture_output=True,
                              timeout=binaries.TIMEOUT, check=False)

    def test_name(self):
        """The name is not read: one not valid UTF-16 reaches location,
        whose usage error answers."""
        done = subprocess.run(["qiven3" + chr(0xD800), "location"], executable=str(binaries.path("qiven3")),
                              capture_output=True, timeout=binaries.TIMEOUT, check=False)
        self.assertEqual((done.returncode, done.stderr), (2, b""))
        self.assertTrue(done.stdout.startswith(b"usage: qiven3 location "), done)

    def test_unwritten(self):
        """show's refusal, not_chosen, answers with status 1; where its
        line cannot be written (standard output a handle open for reading),
        2 and nothing more."""
        args = ("location", "show", "--config", str(self.root / "config"))
        done = self.run_binary(*args)
        self.assertEqual((done.returncode, done.stderr), (1, b""))
        self.assertEqual(done.stdout.count(b"\n"), 1, done)
        closed = self.root / "closed"
        closed.write_bytes(b"")
        with closed.open("rb") as reading:
            failed = subprocess.run([str(binaries.path("qiven3")), *args], stdout=reading,
                                    stderr=subprocess.PIPE, timeout=binaries.TIMEOUT, check=False)
        self.assertEqual((failed.returncode, failed.stderr), (2, b""))
        self.assertEqual(closed.read_bytes(), b"")

    def test_p16(self):
        config = self.root / P16
        self.run_binary("location", "init", str(self.root / "store"), "--config", str(config))
        names = sorted(p.name for p in self.root.iterdir())
        self.assertIn(P16, names)
        self.assertLessEqual(set(names), {P16, "store"})

    def test_unpaired(self):
        done = self.run_binary("location", "init", str(self.root / "store"), "--config",
                               str(self.root / ("caf\ud800")))
        # A request the wire cannot read: its status, 2, and no answer.
        self.assertEqual((done.returncode, done.stdout, done.stderr), (2, b"", b""))
        self.assertEqual(list(self.root.iterdir()), [])

    def test_bound(self):
        """16 arguments reach location, whose usage error answers; 17 are
        past the wire's bound: status 2 and no answer."""
        at = self.run_binary("location", "move", *["x"] * 14)
        self.assertEqual(at.returncode, 2)
        self.assertTrue(at.stdout.startswith(b"usage: "), at)
        past = self.run_binary("location", "move", *["x"] * 15)
        self.assertEqual((past.returncode, past.stdout, past.stderr), (2, b"", b""))
        self.assertEqual(list(self.root.iterdir()), [])


class Wire(unittest.TestCase):
    """qiven3's per-call wire on every system: a request past a bound or
    not UTF-8 is the wire's failure, status 2 with no answer and nothing
    made; one within the bounds reaches its command, whose platform, here
    for a configuration directory no system can make, refuses it: 1."""

    ctest_labels = ("fake",)
    # The binary's bounds: arguments after the name, and the bytes of one
    # and of all.
    COUNT = 16
    BYTES = 3 * 32767

    def setUp(self):
        self.root = scratch(self)

    def run_binary(self, args):
        return subprocess.run([os.fsencode(binaries.path("qiven3")), *args], capture_output=True, cwd=self.root,
                              timeout=binaries.TIMEOUT, check=False)

    def assert_failed(self, done):
        self.assertEqual((done.returncode, done.stdout, done.stderr), (2, b"", b""))
        self.assertEqual(list(self.root.iterdir()), [])

    def off_windows(self, why="a Windows command line holds no request past the bytes' bounds or not UTF-16"):
        if sys.platform == "win32":
            self.skipTest("not applicable: " + why)

    def test_count(self):
        self.assert_failed(self.run_binary([b"location", b"show", *[b"x"] * (self.COUNT - 1)]))

    def test_bytes(self):
        self.off_windows()
        fixed = [b"location", b"show", b"--config"]
        within = self.BYTES - sum(len(a) for a in fixed)
        at = self.run_binary([*fixed, b"a" * within])
        self.assertEqual((at.returncode, at.stderr), (1, b""))
        # The total past by one byte, each argument within.
        self.assert_failed(self.run_binary([*fixed, b"a" * (within + 1)]))
        # One argument past.
        self.assert_failed(self.run_binary([b"a" * (self.BYTES + 1)]))

    def test_not_utf8(self):
        self.off_windows()
        for bad in (b"caf\xe9", b"\xff", b"\xed\xa0\x80"):
            with self.subTest(argument=bad):
                self.assert_failed(self.run_binary([b"location", b"show", b"--config", bad]))

    def test_full(self):
        """A usage error's answer, written or not, ends with status 2; on a
        full standard output nothing reaches standard error."""
        self.off_windows("Binary.test_unwritten fails the answer's write there")
        if not Path("/dev/full").exists():
            self.skipTest("not applicable: the system has no full device")
        with open("/dev/full", "wb") as full:
            done = subprocess.run([os.fsencode(binaries.path("qiven3")), b"move"], stdout=full,
                                  stderr=subprocess.PIPE, cwd=self.root, timeout=binaries.TIMEOUT, check=False)
        self.assertEqual((done.returncode, done.stderr), (2, b""))


if __name__ == "__main__":
    unittest.main()
