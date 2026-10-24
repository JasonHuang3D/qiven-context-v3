"""The runner's routes through a process: qiven3_runner_demo's cases, its
--list and its refusals, each status as CTest reads it."""

import signal
import sys
import unittest

from tests import SKIP_CODE
from tests.drive import binaries
from tests.drive.session import build_option

DEMO = "qiven3_runner_demo"


def lines(data):
    return data.decode("utf-8").splitlines()


class Routes(unittest.TestCase):
    ctest_reads = ("qiven3_runner_demo",)

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

    def test_held(self):
        """A case ending with a counted arena holding bytes fails."""
        ran = binaries.run(DEMO, ["--run", "demo.held"])
        self.assertEqual(ran.status, 1)
        self.assertEqual(lines(ran.out),
                         ["held at the case's end: arena 1, 16 bytes", "FAIL demo.held: 1 failed"])

    def test_crash(self):
        """A crash is not caught: the system's status, or, in a build under
        a sanitizer whose runtime reports the access and exits 1, that
        status and its report: UndefinedBehaviorSanitizer's, which stops
        first where it is on, else AddressSanitizer's."""
        ran = binaries.run(DEMO, ["--run", "demo.crash"])
        self.assertEqual(ran.out, b"")
        sanitize = build_option("QIVEN3_SANITIZE") or ""
        if "undefined" in sanitize:
            self.assertEqual(ran.status, 1)
            self.assertIn(b"runtime error: store to null pointer", ran.err)
        elif "address" in sanitize:
            self.assertEqual(ran.status, 1)
            self.assertIn(b"AddressSanitizer", ran.err)
        elif sys.platform == "win32":
            self.assertEqual(ran.status, 0xC0000005)  # STATUS_ACCESS_VIOLATION
        else:
            self.assertEqual(ran.status, -signal.SIGSEGV)

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
                "demo.held",
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
