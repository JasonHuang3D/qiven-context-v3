"""The drives' standard streams are binary: each reply is one line ending in
LF, with no CR, on every system, so the bytes a test compares do not depend
on it. A request ending in CRLF is read as one ending in LF."""

import json
import shutil
import sys
import tempfile
import unittest

from tests.drive import binaries
from tests.drive.session import FAKE, WINDOWS, wire

# A result, a platform error and a request error, the last ending in CRLF.
REQUESTS = ({"op": "name"}, {"op": "adapter_for", "system": "linux"})
CRLF_REQUEST = wire({"op": "no_such_op"})[:-1] + b"\r\n"


class Streams:
    DRIVE = None

    def args(self):
        return []

    def test_replies_end_in_lf(self):
        data = b"".join(wire(r) for r in REQUESTS) + CRLF_REQUEST
        ran = binaries.run(self.DRIVE, self.args(), data)
        self.assertEqual(ran.status, 0, ran.err)
        self.assertNotIn(b"\r", ran.out)
        self.assertTrue(ran.out.endswith(b"\n"), ran.out)
        lines = ran.out[:-1].split(b"\n")
        self.assertEqual(len(lines), 3, ran.out)
        replies = [json.loads(line) for line in lines]
        self.assertEqual([reply["ok"] for reply in replies], [True, False, False])
        self.assertEqual([reply["error"]["kind"] for reply in replies[1:]], ["platform", "request"])


class Fake(Streams, unittest.TestCase):
    DRIVE = FAKE
    ctest_labels = ("fake",)

    def args(self):
        root = tempfile.mkdtemp(prefix="qiven3-s-")
        self.addCleanup(shutil.rmtree, root, True)
        return ["--root", root]


class Windows(Streams, unittest.TestCase):
    DRIVE = WINDOWS

    def setUp(self):
        if sys.platform != "win32":
            self.skipTest("not applicable: the Windows drive is built on Windows alone")
        super().setUp()


if __name__ == "__main__":
    unittest.main()
