"""process: a run past its limit ends its whole tree. The tree's ending fails in
silence, taskkill's status unread and system.Failed taken as no error, and a
limit is rarely reached in use: a process left behind goes on holding memory and
writing. So this case runs a program whose child holds the run's pipe, the form
that kept subprocess.run past its timeout."""

import sys
import time
import unittest
from pathlib import Path

from foundation.devkit.python import process
from foundation.devkit.python import system

ROOT = Path(__file__).resolve().parents[4]
LIMIT = 3  # seconds: the program has started its child and said so long before
LEFT = 10  # seconds the child's end may take to show
PROGRAM = (f"import sys, time; sys.path.append({str(ROOT)!r}); "
           "from foundation.devkit.python import process; "
           "child = process.start([sys.executable, '-I', '-c', 'import time; time.sleep(60)'], "
           "out=process.INHERIT, err=process.INHERIT); "
           "print(child.pid, flush=True); time.sleep(60)")


class Limit(unittest.TestCase):
    ctest_labels = ("fake",)

    @unittest.skipUnless(system.WINDOWS, "system.end_tree has no back end off Windows")
    def test_tree_ended(self):
        """TimedOut, naming the limit and holding what the program wrote, which
        comes only once the pipe's holder is gone; the child no longer runs."""
        with self.assertRaises(process.TimedOut) as caught:
            process.run([sys.executable, "-I", "-c", PROGRAM], limit=LIMIT)
        self.assertEqual(caught.exception.limit, LIMIT)
        self.assertTrue(caught.exception.out, "the program's child outlived the limit and holds the pipe")
        child = int(caught.exception.out)
        deadline = time.monotonic() + LEFT
        while system.alive(child) and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertFalse(system.alive(child), f"the program's child {child} outlived the limit")


if __name__ == "__main__":
    unittest.main()
