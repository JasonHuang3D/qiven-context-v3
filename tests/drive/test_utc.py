"""utc reads no time zone: qiven3_unit_utc's cases, each run as a process
under TZ CST-8 and again under TZ UTC0, pass under both, and the zone case
reports the C runtime's local offset, 28,800 seconds and 0, so the zone
was the process's while the vectors gave the same results. The fault run
may skip, as it does in a build without fault injection.
"""

import os
import subprocess
import unittest

from tests import SKIP_CODE
from tests.drive import binaries

CASES = "qiven3_unit_utc"
# Each zone and the offset from UTC, in seconds, it gives the C runtime.
ZONES = {"CST-8": 28800, "UTC0": 0}


def run(args, zone):
    """The case binary with `args`, under TZ `zone`."""
    env = dict(os.environ)
    env["TZ"] = zone
    done = subprocess.run([str(binaries.path(CASES)), *args], capture_output=True, env=env,
                          timeout=binaries.TIMEOUT, check=False)
    return binaries.Ran(done.returncode, done.stdout, done.stderr)


class UtcZones(unittest.TestCase):
    ctest_labels = ("fake",)
    ctest_reads = ("qiven3_unit_utc",)

    def test_cases_under_each_zone(self):
        listed = run(["--list"], "UTC0")
        self.assertEqual(listed.status, 0, listed)
        names = [line.split("\t")[0] for line in listed.out.decode("ascii").splitlines() if line]
        self.assertIn("utc.zone", names)
        self.assertIn("utc.vectors", names)
        for zone, offset in ZONES.items():
            for name in names:
                with self.subTest(zone=zone, case=name):
                    ran = run(["--run", name], zone)
                    # The fault run skips in a build without fault injection.
                    allowed = (0, SKIP_CODE) if name == "utc.faults" else (0,)
                    self.assertIn(ran.status, allowed, ran)
                    if name == "utc.zone":
                        self.assertIn(f"utc.zone: local offset {offset} s".encode("ascii"), ran.out)


if __name__ == "__main__":
    unittest.main()
