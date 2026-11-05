"""K10: every binary of the build runs the diagnostic entry. Each binary
listed in the build's binaries.txt, which qiven_executable writes, started
with QIVEN_DIAG_PROBE=1, prints "diag: entry" from its entry, before main,
and exits 0. The control: a binary without the entry, this interpreter,
caught.
"""

import os
import sys
import unittest
from pathlib import Path

from foundation.devkit.python import process
from foundation.devkit.test.runner import binaries

PROBE = b"diag: entry\n"


def probe_problem(command):
    """None when the binary's entry answers its probe, else what it gave."""
    env = dict(os.environ, QIVEN_DIAG_PROBE="1")
    # The test tree's library, which qiven_diag_fault_import imports, by
    # the loader's path on Linux: the binary holds no run path (K11).
    env["LD_LIBRARY_PATH"] = str(Path(command[0]).parent)
    done = process.run(command, env=env)
    if done.status != 0 or done.out != PROBE:
        return f"{command[0]}: {done.named}, out {done.out[:80]!r}"
    return None


class Entry(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_K10_probe(self):
        problems = [probe_problem([str(f)]) for f in sorted(binaries.listed())]
        self.assertEqual([p for p in problems if p], [])

    def test_K10_controls(self):
        self.assertIsNotNone(probe_problem([sys.executable, "-I", "-c", "pass"]))


if __name__ == "__main__":
    unittest.main()
