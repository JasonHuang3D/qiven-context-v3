"""The binary's command line on Windows: wmain takes the arguments as
UTF-16 and the Windows adapter converts each strictly to UTF-8
(WindowsArguments), through qiven3-drive-windows' operation arguments and
through the qiven3 binary itself, P16's directory, cafe with U+00EB, among
them.

The conversion's budget is its measure, argc + 1 pointers and each
argument's UTF-8 and its null, which its arena's peak meets; a command
line holds at most 32,767 units and 16,384 arguments; an unpaired
surrogate is PlatformError path, naming the argument; under QIVEN3_FAULTS
each of its two allocations fails in turn.
"""

import subprocess
import sys
import unittest

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
    """The conversion's bytes: the pointers, then the text and its nulls."""
    return (len(texts) + 1) * POINTER + sum(len(t.encode("utf-8")) + 1 for t in texts)


class WindowsOnly(unittest.TestCase):
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
        self.assertEqual([bytes.fromhex(a) for a in result["argv"]], [t.encode("utf-8") for t in self.TEXTS])
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

    def test_short(self):
        reply = self.convert(self.TEXTS, budget=measure(self.TEXTS) - 1)
        self.assertFalse(reply["ok"])
        self.assertEqual((reply["error"]["kind"], reply["error"]["code"]), ("memory", "budget"))
        self.assertEqual(reply["memory"]["left"], 0)

    def test_bound(self):
        widest = ["\u20ac" * UNITS]
        reply = self.convert(widest)
        self.assertTrue(reply["ok"], reply.get("error"))
        self.assertEqual(reply["memory"]["peak"], 2 * POINTER + 3 * UNITS + 1)
        reply = self.convert([""] * MOST)
        self.assertTrue(reply["ok"], reply.get("error"))
        self.assertEqual(reply["memory"]["peak"], (MOST + 1) * POINTER + MOST)
        for past in (["\u20ac" * UNITS, "a"], [""] * (MOST + 1)):
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
        import tempfile
        from pathlib import Path

        scratch = tempfile.TemporaryDirectory(prefix="qiven3-arguments-")
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)

    def run_binary(self, *args):
        return subprocess.run([str(binaries.path("qiven3")), *args], capture_output=True,
                              timeout=binaries.TIMEOUT, check=False)

    def test_p16(self):
        config = self.root / P16
        self.run_binary("location", "init", str(self.root / "store"), "--config", str(config))
        names = sorted(p.name for p in self.root.iterdir())
        self.assertIn(P16, names)
        self.assertLessEqual(set(names), {P16, "store"})

    def test_unpaired(self):
        done = self.run_binary("location", "init", str(self.root / "store"), "--config",
                               str(self.root / ("caf\ud800")))
        self.assertEqual((done.returncode, done.stdout, done.stderr), (1, b"", b""))
        self.assertEqual(list(self.root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
