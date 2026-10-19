"""The runner's routes through a process: qiven3_runner_demo's cases, its
--list and its refusals, each status as CTest reads it."""

import signal
import sys
import unittest

from tests import SKIP_CODE
from tests.drive import binaries

DEMO = "qiven3_runner_demo"


def lines(data):
    return data.decode("utf-8").splitlines()


class Routes(unittest.TestCase):
    def test_pass(self):
        ran = binaries.run(DEMO, ["--run", "demo.pass"])
        self.assertEqual(ran.status, 0)
        self.assertEqual(lines(ran.out), ["PASS demo.pass"])

    def test_fail(self):
        ran = binaries.run(DEMO, ["--run", "demo.fail"])
        self.assertEqual(ran.status, 1)
        out = lines(ran.out)
        self.assertEqual(len(out), 2)
        self.assertRegex(out[0], r"runner_demo\.cpp\(\d+\): CHECK failed: 1 \+ 1 == 3$")
        self.assertEqual(out[1], "FAIL demo.fail: 1 failed")

    def test_require(self):
        ran = binaries.run(DEMO, ["--run", "demo.require"])
        self.assertEqual(ran.status, 1)
        out = lines(ran.out)
        self.assertRegex(out[0], r"CHECK failed: false$")
        self.assertEqual(out[1:], ["stopped by a failed REQUIRE", "FAIL demo.require: 1 failed"])

    def test_exception(self):
        ran = binaries.run(DEMO, ["--run", "demo.exception"])
        self.assertEqual(ran.status, 1)
        self.assertEqual(
            lines(ran.out), ["exception: the demo's exception", "FAIL demo.exception: 1 failed"]
        )

    def test_skip(self):
        ran = binaries.run(DEMO, ["--run", "demo.skip"])
        self.assertEqual(ran.status, SKIP_CODE)
        self.assertEqual(lines(ran.out), ["SKIP demo.skip: not applicable: the demo's skip"])

    def test_crash(self):
        ran = binaries.run(DEMO, ["--run", "demo.crash"])
        if sys.platform == "win32":
            self.assertEqual(ran.status, 0xC0000005)  # STATUS_ACCESS_VIOLATION
        else:
            self.assertEqual(ran.status, -signal.SIGSEGV)
        self.assertEqual(ran.out, b"")

    def test_list(self):
        ran = binaries.run(DEMO, ["--list"])
        self.assertEqual(ran.status, 0)
        self.assertEqual(ran.err, b"")
        self.assertEqual(
            lines(ran.out),
            [
                "demo.crash",
                "demo.exception",
                "demo.fail",
                "demo.pass",
                "demo.require",
                "demo.serial\tserial",
                "demo.skip",
            ],
        )

    def test_unknown_name(self):
        ran = binaries.run(DEMO, ["--run", "demo.none"])
        self.assertEqual(ran.status, 2)
        self.assertEqual(ran.out, b"")
        self.assertEqual(lines(ran.err), ["no such case: demo.none"])

    def test_usage(self):
        for args in ([], ["--run"], ["--list", "demo.pass"], ["--help"]):
            with self.subTest(args=args):
                ran = binaries.run(DEMO, args)
                self.assertEqual(ran.status, 2)
                self.assertEqual(lines(ran.err), ["usage: --list | --run NAME"])
