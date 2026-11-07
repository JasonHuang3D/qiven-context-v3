"""junit: each row of the reading, on planted JUnit files in CTest's form. A
misread outcome wrong in silence counts a failed test passed, or a test that
never started as the test's own red."""

import tempfile
import unittest
from pathlib import Path
from xml.sax.saxutils import escape, quoteattr

from foundation.devkit.test.gate import junit


def case(name, status, told=None, message="", out="", time="1.25"):
    inner = f"<{told} message={quoteattr(message)}/>" if told else ""
    return (f"<testcase name={quoteattr(name)} classname={quoteattr(name)} time=\"{time}\" status=\"{status}\">"
            f"{inner}<system-out>{escape(out)}</system-out></testcase>")


CASES = [
    ("pass", case("pass", "run", out="ok\n"), "passed"),
    ("skip_code", case("skip_code", "notrun", "skipped", "SKIP_RETURN_CODE=125",
                       "SKIP skip_code: not applicable: no cdb\nstatus: 125 skipped: the runner's skip code\n"),
     "skipped"),
    ("skip_regex", case("skip_regex", "notrun", "skipped", " SKIP_REGULAR_EXPRESSION_MATCHED "), "skipped"),
    ("timeout", case("timeout", "fail", "failure", "Timeout"), "timeout"),
    ("hang", case("hang", "fail", "failure", "Failed",
                  "case\nstatus: 124 hang: the watcher's: past its limit, killed; t past 3 s\n\n"), "timeout"),
    ("segfault", case("segfault", "fail", "failure", "SEGFAULT"), "crashed"),
    ("exit_code", case("exit_code", "fail", "failure", "Exit code 0xc0000409"), "crashed"),
    ("crash_line", case("crash_line", "fail", "failure", "Failed",
                        "status: 0xC0000005 STATUS_ACCESS_VIOLATION: a read\n"), "crashed"),
    ("abort_line", case("abort_line", "fail", "failure", "Failed", "status: 3 abort: Windows\n"), "crashed"),
    ("failed", case("failed", "fail", "failure", "Failed", "status: 1 failed: a case failed\n"), "failed"),
    ("no_line", case("no_line", "fail", "failure", "Failed", "status: 124x\n"), "failed"),
    ("no_exe", case("no_exe", "notrun", "skipped", "Unable to find executable"), junit.NOT_STARTED),
    ("disabled", case("disabled", "disabled"), junit.NOT_STARTED),
]


class Reading(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="qiven-gate-junit-")
        self.addCleanup(self.scratch.cleanup)
        self.path = Path(self.scratch.name) / "run.xml"

    def plant(self, cases):
        body = "".join(cases)
        self.path.write_text(f'<?xml version="1.0" encoding="UTF-8"?>\n<testsuite name="Linux-c++" tests="1">'
                             f"{body}</testsuite>\n", encoding="utf-8")

    def test_rows(self):
        """Each status, message and last line read to its outcome; the seconds read."""
        self.plant([text for _, text, _ in CASES])
        ran = junit.read(self.path, [name for name, _, _ in CASES] + ["absent"])
        for name, _, expected in CASES:
            with self.subTest(name=name):
                self.assertEqual(ran[name].outcome, expected)
        self.assertEqual(ran["pass"].seconds, 1.25)
        self.assertEqual(ran["skip_regex"].why, "SKIP_REGULAR_EXPRESSION_MATCHED")
        self.assertEqual(ran["skip_code"].why, "not applicable: no cdb")
        self.assertEqual(ran["absent"], junit.Ran(junit.NOT_STARTED, None, "no testcase in the JUnit"))

    def test_no_junit(self):
        """No JUnit, or one that does not parse: every selected test not started."""
        for planted in (None, "<testsuite><testcase"):
            with self.subTest(planted=planted):
                if planted is not None:
                    self.path.write_text(planted, encoding="utf-8")
                ran = junit.read(self.path, ["a", "b"])
                self.assertEqual({r.outcome for r in ran.values()}, {junit.NOT_STARTED})
                self.assertEqual(sorted(ran), ["a", "b"])
