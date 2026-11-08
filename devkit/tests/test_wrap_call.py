"""The call base past its limit: none of its program's tree is left running,
whose cost is memory held by processes nobody waits on, and the answer is
slow, never missing."""

import ctypes
import os
import sys
import tempfile
import threading
import time
import unittest
from ctypes import wintypes
from pathlib import Path

from qiven_devkit.wrap import call

# A program that starts a child, each writing its own pid; then "stay" sleeps
# and "leave" ends at once, its child holding the output pipe.
TREE = '''import os, subprocess, sys, time
pids, mode = sys.argv[1], sys.argv[2]
def mark(suffix):
    with open(pids + suffix + ".tmp", "w") as file:
        file.write(str(os.getpid()))
    os.replace(pids + suffix + ".tmp", pids + suffix)
if mode == "child":
    mark(".child")
    time.sleep(120)
else:
    subprocess.Popen([sys.executable, __file__, pids, "child"])
    mark(".program")
    if mode == "stay":
        time.sleep(120)
'''

LIMIT = 8  # seconds: both pythons start and write their pids well inside it
SYNCHRONIZE = 0x00100000
WAIT_OBJECT_0 = 0

if sys.platform == "win32":
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)


@unittest.skipUnless(sys.platform == "win32", "wrap ends a tree on Windows only")
class PastItsLimit(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.program = self.root / "tree.py"
        self.program.write_text(TREE, encoding="utf-8")

    def check_tree_ended(self, mode):
        pids = self.root / mode
        done = {}

        def run():
            try:
                call.call("tree", sys.executable, [self.program, pids, mode], env=dict(os.environ),
                          cwd=self.root, limit=LIMIT)
            except BaseException as error:  # the main thread reads it
                done["error"] = error

        started = time.monotonic()
        thread = threading.Thread(target=run)
        thread.start()
        handles = []
        try:
            for suffix in (".program", ".child"):
                marked = Path(f"{pids}{suffix}")
                while not marked.is_file() and thread.is_alive():
                    time.sleep(0.05)
                self.assertTrue(marked.is_file(), f"{suffix[1:]} never wrote its pid")
                handle = kernel32.OpenProcess(SYNCHRONIZE, False, int(marked.read_text()))
                if handle:  # a program that has ended already ("leave") opens no more
                    handles.append(handle)
            thread.join(LIMIT + 60)
            self.assertFalse(thread.is_alive(), "the call did not return")
            error = done.get("error")
            self.assertIsInstance(error, call.Slow, f"not slow: {error!r}")
            self.assertEqual((error.call, error.limit), ("tree", LIMIT))
            self.assertGreaterEqual(time.monotonic() - started, LIMIT)
            for handle in handles:
                self.assertEqual(kernel32.WaitForSingleObject(handle, 10_000), WAIT_OBJECT_0,
                                 "a process of the tree is still running")
            self.assertTrue(handles)
        finally:
            for handle in handles:
                kernel32.CloseHandle(handle)

    def test_program_and_child_running(self):
        """Both still run at the limit: both are ended."""
        self.check_tree_ended("stay")

    def test_program_gone_child_holds_its_pipe(self):
        """The program ended, its child holding its pipe past the limit: the
        child, which no parent chain reaches now, is ended."""
        self.check_tree_ended("leave")


if __name__ == "__main__":
    unittest.main()
