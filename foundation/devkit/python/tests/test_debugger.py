"""debugger: the hang's debugger and the hang's lines, the cases that are
debugger's own of the watcher's hang path. Off CI (no GITHUB_ACTIONS)
hang_debugger is the pinned cdb, or none with the pin's reason, never a cdb
on PATH, which the pins refuse; in a CI job on Windows (GITHUB_ACTIONS
"true") the image's cdb, unpinned, how naming the version its -version
gives and its path, or, with none on PATH nor in the SDK's Debuggers/x64,
NotFound saying where it looked. The cdb is a stand-in, a one-line cdb.cmd on
PATH that gives a -version, the real start of a program. hang_lines keeps 16 threads and 199 lines, with [cut], and a line within
answer's bound: a broken bound floods the watcher's answer in silence.
"""

import os
import re
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from foundation.devkit.python import answer
from foundation.devkit.python import debugger
from foundation.devkit.python import system
from foundation.devkit.python.answer import LINE_POINTS

def stand_in(root):
    """A stand-in for a CI image's cdb in root/bin, its -version alone: its
    path."""
    (root / "bin").mkdir()
    cmd = root / "bin" / "cdb.cmd"
    cmd.write_text("@echo cdb version 10.0.99999.1\n", encoding="ascii")
    return cmd


class Hang(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_pinned_here(self):
        """Off CI (no GITHUB_ACTIONS), the hang's debugger is the pinned
        one, or none with the pin's reason: never a cdb on PATH, which the
        pins refuse."""
        if not system.WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            standin = stand_in(Path(root))
            with mock.patch.dict(os.environ, {"PATH": f"{standin.parent}{os.pathsep}{os.environ.get('PATH', '')}"}):
                os.environ.pop(debugger.CI_VARIABLE, None)
                try:
                    path, how = debugger.hang_debugger()
                except debugger.NotFound as e:
                    path, how = None, str(e)
            self.assertNotIn(str(standin.parent), str(path), how)
            self.assertNotIn(standin.parent.as_posix(), str(how))
            if path is not None:
                self.assertIsNone(how)

    def test_ci_image_cdb(self):
        """In a CI job on Windows the hang's debugger is the image's cdb,
        unpinned (here a stand-in on PATH): how naming the version its
        -version gives and its path."""
        if not system.WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            standin = stand_in(Path(root))
            with mock.patch.dict(os.environ, {debugger.CI_VARIABLE: "true", "PATH": str(standin.parent)}):
                path, how = debugger.hang_debugger()
            self.assertEqual(Path(path).parent, standin.parent, how)
            self.assertRegex(how, r"^by the image's cdb 10\.0\.99999\.1 at "
                             + re.escape(standin.parent.as_posix()) + r"/cdb\.\w+, unpinned \(CI\)$")

    def test_ci_no_cdb(self):
        """In a CI job on Windows with no cdb on PATH nor in the SDK's
        Debuggers/x64: NotFound, saying where it looked."""
        if not system.WINDOWS:
            self.skipTest("cdb is Windows'")
        with tempfile.TemporaryDirectory() as root:
            empty, sdk = Path(root) / "empty", Path(root) / "sdk"
            empty.mkdir()
            sdk.mkdir()
            changes = {debugger.CI_VARIABLE: "true", "PATH": str(empty), "WindowsSdkDir": str(sdk)}
            with mock.patch.dict(os.environ, changes), self.assertRaises(debugger.NotFound) as caught:
                debugger.hang_debugger()
            self.assertEqual(str(caught.exception),
                             f"no cdb: none on PATH, none at {(sdk / 'Debuggers' / 'x64' / 'cdb.exe').as_posix()}, "
                             "the SDK's root by WindowsSdkDir")

    def test_hang_lines_bound(self):
        """The stacks' reading keeps 16 threads and 199 lines, with [cut]."""
        header = "   {i}  Id: 1a2b.{i:x} Suspend: 1 Teb: 00000000`00000000 Unfrozen"
        text = "0:000> cdb: Reading initial command '~*kn 32; qd'\n"
        text += "".join(header.format(i=i + 1) + "\n" + "#0 frame\n" * 3 for i in range(20))
        threads, lines = debugger.hang_lines(text)
        self.assertEqual(threads, 20)
        self.assertEqual(debugger.stacks_of(lines), debugger.HANG_THREADS)
        self.assertTrue(lines[-1].startswith(answer.CUT), lines[-1])
        threads, lines = debugger.hang_lines(text.replace("#0 frame\n", "#0 frame\n" * 20))
        self.assertEqual(len(lines), debugger.HANG_LINES - 1)
        self.assertTrue(lines[-1].startswith(answer.CUT), lines[-1])
        self.assertEqual(answer.clip("x" * 400), "x" * (LINE_POINTS - 5) + answer.CUT)


if __name__ == "__main__":
    unittest.main()
