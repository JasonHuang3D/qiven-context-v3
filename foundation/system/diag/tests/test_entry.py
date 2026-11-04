"""K10: every binary of the build runs the diagnostic entry. Each binary
listed in the build's binaries.txt, which qiven_executable writes, started
with QIVEN_DIAG_PROBE=1, prints "diag: entry" from its entry, before main,
and exits 0. The control: a binary without the entry, this interpreter,
caught.
"""

import os
import subprocess
import sys
import unittest
from pathlib import Path

from foundation.devkit.test.runner import binaries

PROBE = b"diag: entry\n"


def listed(build):
    """binaries.txt's files, as resolved paths."""
    text = (build / "binaries.txt").read_text(encoding="utf-8")
    return {Path(line).resolve() for line in text.splitlines() if line}


def probe_problem(command):
    """None when the binary's entry answers its probe, else what it gave."""
    env = dict(os.environ, QIVEN_DIAG_PROBE="1")
    # The test tree's library, which qiven_diag_fault_import imports, by
    # the loader's path on Linux: the binary holds no run path (K11).
    env["LD_LIBRARY_PATH"] = str(Path(command[0]).parent)
    done = subprocess.run(command, capture_output=True, env=env, stdin=subprocess.DEVNULL,
                          timeout=binaries.TIMEOUT, check=False)
    if done.returncode != 0 or done.stdout != PROBE:
        return f"{command[0]}: {binaries.Ran(done.returncode, b'', b'').named}, out {done.stdout[:80]!r}"
    return None


class Entry(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_K10_probe(self):
        build = binaries.directory("QIVEN_BUILD")
        problems = [probe_problem([str(f)]) for f in sorted(listed(build))]
        self.assertEqual([p for p in problems if p], [])

    def test_K10_controls(self):
        self.assertIsNotNone(probe_problem([sys.executable, "-I", "-c", "pass"]))


if __name__ == "__main__":
    unittest.main()
