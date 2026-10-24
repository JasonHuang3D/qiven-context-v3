"""The watcher, tools/watch.py, on commands of this interpreter: the
environment it gives the case, the status line by tools/status.py's table,
its exit with the case's status (a signal as 128 plus its number), its
limit's 124, its error mode on Windows, and the table's own rows."""

import os
import signal
import subprocess
import sys
import time
import unittest

from tests.drive import binaries
from tools import status as status_table

WINDOWS = sys.platform == "win32"
LIMIT = 30


def watch(code, name="watch.case", limit=LIMIT, env=None):
    """Runs `code` by this interpreter behind the watcher: its status, out
    and err lines, and seconds."""
    start = time.monotonic()
    done = subprocess.run(binaries.watched([sys.executable, "-I", "-c", code], name, limit), capture_output=True,
                          env=env, stdin=subprocess.DEVNULL, timeout=binaries.TIMEOUT, check=False)
    return (done.returncode, done.stdout.decode("utf-8", "replace").splitlines(),
            binaries.unbounded(done.stderr).decode("utf-8", "replace").splitlines(), time.monotonic() - start)


class Watch(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_statuses(self):
        for code, status in ((0, 0), (1, 1), (125, 125), (77, 77)):
            ran, out, err, _ = watch(f"import sys; print('out'); sys.stderr.write('err\\n'); sys.exit({code})")
            self.assertEqual((ran, out, err), (status, ["out"], ["err", status_table.line(status)]))
        self.assertEqual(status_table.line(77), "status: 77 unnamed")

    def test_environment(self):
        build = binaries.directory("QIVEN3_BUILD")
        code = ("import os; print(os.environ['QIVEN3_WATCHER'] == str(os.getppid())); "
                "print(os.environ['QIVEN3_DUMP_DIR']); print(os.environ['QIVEN3_LOG'])")
        ran, out, _, _ = watch(code, name="watch.env/case")
        self.assertEqual(ran, 0)
        self.assertEqual(out[0], "True")
        self.assertEqual(os.path.normcase(out[1]), os.path.normcase(str(build / "dumps")))
        self.assertEqual(os.path.normcase(out[2]), os.path.normcase(str(build / "logs" / "watch.env_case.jsonl")))

    def test_limit(self):
        ran, _, err, seconds = watch("import time; time.sleep(60)", limit=1)
        self.assertEqual(ran, 124)
        self.assertTrue(err[-1].startswith(status_table.line(124) + "; watch.case past 1 s"), err)
        self.assertLess(seconds, 20)

    def test_system_status(self):
        if not WINDOWS:
            self.skipTest("Windows' statuses")
        for status in (0xC0000005, 0xC0000409, 0xC00000FD, 0xE06D7363):
            ran, _, err, _ = watch(f"import os; os._exit({status - (1 << 32)})")
            self.assertEqual((ran, err), (status, [status_table.line(status)]))
            self.assertNotIn(status_table.UNNAMED, err[0])

    def test_signal(self):
        if WINDOWS:
            self.skipTest("Linux's signals")
        for number in (signal.SIGTERM, signal.SIGSEGV, signal.SIGKILL):
            ran, _, err, _ = watch(f"import os; os.kill(os.getpid(), {int(number)})")
            self.assertEqual((ran, err), (128 + number, [status_table.line(-number)]))
            self.assertTrue(err[0].startswith(f"status: signal {int(number)} {number.name}: "), err)

    def test_error_mode(self):
        """The case inherits the entry's error mode from the watcher."""
        if not WINDOWS:
            self.skipTest("Windows' error mode")
        ran, out, _, _ = watch("import ctypes; print(ctypes.WinDLL('kernel32').GetErrorMode())")
        self.assertEqual(ran, 0)
        self.assertEqual(int(out[0]) & 0x8003, 0x8003)

    def test_usage(self):
        done = subprocess.run(binaries.watched([], "watch.none")[:-1], capture_output=True, timeout=binaries.TIMEOUT,
                              check=False)
        self.assertEqual(done.returncode, 2)


class Table(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_rows(self):
        self.assertEqual(status_table.line(0), "status: 0 passed")
        self.assertEqual(status_table.line(0xC0000135),
                         "status: 0xC0000135 STATUS_DLL_NOT_FOUND: a library the binary imports was not found")
        self.assertEqual(status_table.line(-11),
                         "status: signal 11 SIGSEGV: Linux: a death by that signal, read from the wait status")
        self.assertEqual(len(status_table.TABLE), 20)
        self.assertEqual(len(status_table.SIGNALS), 9)
        self.assertTrue(all(name and name != status_table.UNNAMED for name, _ in status_table.TABLE.values()))

    def test_script(self):
        done = subprocess.run([sys.executable, "-I", str(binaries.ROOT / "tools" / "status.py"), "0xc0000409", "-6",
                               "3"], capture_output=True, timeout=binaries.TIMEOUT, check=True)
        self.assertEqual(done.stdout.decode().splitlines(), [status_table.line(0xC0000409), status_table.line(-6),
                                                            status_table.line(3)])


if __name__ == "__main__":
    unittest.main()
