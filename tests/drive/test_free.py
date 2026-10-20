"""A wrong free ends the process: qiven3_free_demo's cases, each run as a
process. A wrong pointer given back to a pool or a heap fails fast, with
std::terminate's status, abort's: 3 on Windows and SIGABRT elsewhere, and
writes nothing, so a case never passes its wrong free. Under
QIVEN3_SYSTEM_HEAP a pool's slot is the system heap's, so AddressSanitizer
reports the pool's wrong free, and a touch of a freed block, of an arena's
block after its end, or past a block, each with its report and its
runtime's status 1."""

import signal
import sys
import unittest

from tests.drive import binaries
from tests.drive.session import build_option

DEMO = "qiven3_free_demo"

# Each pool case, and the report AddressSanitizer gives it when the pool's
# slots are the system heap's, with the lines written before it: another
# pool's slot is freed there, and its own pool's free of it is the second.
NOT_MALLOCED = b"attempting free on address which was not malloc()-ed"
POOL = {
    "free.pool_foreign": (b"attempting double-free", ["past the wrong free"]),
    "free.pool_interior": (NOT_MALLOCED, []),
    "free.pool_one_past": (NOT_MALLOCED, []),
    "free.pool_past_span": (NOT_MALLOCED, []),
}
HEAP = ("free.heap_foreign", "free.heap_interior", "free.heap_interior_forged", "free.heap_one_past",
        "free.heap_past_span", "free.heap_twice")
TOUCH = {
    "asan.pool_use_after_free": b"heap-use-after-free",
    "asan.heap_use_after_free": b"heap-use-after-free",
    "asan.arena_use_after_end": b"heap-use-after-free",
    "asan.arena_past_block": b"heap-buffer-overflow",
}


def option_on(name):
    value = build_option(name)
    return value is not None and value.upper() in ("ON", "1", "TRUE", "YES", "Y")


class Free(unittest.TestCase):
    ctest_labels = ("fake",)

    def assert_fails_fast(self, ran):
        if sys.platform == "win32":
            self.assertEqual(ran.status, 3)
        else:
            self.assertEqual(ran.status, -signal.SIGABRT)
        self.assertEqual(ran.out, b"")
        self.assertEqual(ran.err, b"")

    def assert_reported(self, ran, report, out=()):
        self.assertEqual(ran.status, 1)
        self.assertEqual(ran.out.decode("utf-8").splitlines(), list(out))
        self.assertIn(b"ERROR: AddressSanitizer: " + report, ran.err)

    def test_heap(self):
        for name in HEAP:
            with self.subTest(case=name):
                self.assert_fails_fast(binaries.run(DEMO, ["--run", name]))

    def test_pool(self):
        system_heap = option_on("QIVEN3_SYSTEM_HEAP")
        address = "address" in (build_option("QIVEN3_SANITIZE") or "")
        if system_heap and not address:
            self.skipTest("not applicable: the system heap's wrong free is seen by no tool here")
        for name, (report, out) in POOL.items():
            with self.subTest(case=name):
                ran = binaries.run(DEMO, ["--run", name])
                if system_heap:
                    self.assert_reported(ran, report, out)
                else:
                    self.assert_fails_fast(ran)

    def test_touch(self):
        if not option_on("QIVEN3_SYSTEM_HEAP") or "address" not in (build_option("QIVEN3_SANITIZE") or ""):
            self.skipTest("not applicable: built without AddressSanitizer and QIVEN3_SYSTEM_HEAP")
        for name, report in TOUCH.items():
            with self.subTest(case=name):
                self.assert_reported(binaries.run(DEMO, ["--run", name]), report)

    def test_list(self):
        ran = binaries.run(DEMO, ["--list"])
        self.assertEqual(ran.status, 0)
        self.assertEqual(ran.out.decode("utf-8").splitlines(), sorted([*POOL, *HEAP, *TOUCH]))


if __name__ == "__main__":
    unittest.main()
