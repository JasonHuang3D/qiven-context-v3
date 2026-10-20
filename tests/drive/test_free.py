"""A wrong free ends the process, and a touch of what an allocator holds is
AddressSanitizer's report: qiven3_free_demo's cases, each run as a process.

A wrong pointer given back to a pool or a heap fails fast, with
std::terminate's status, abort's: 3 on Windows and SIGABRT elsewhere, and
writes nothing, so a case never passes its wrong free. Under
QIVEN3_SYSTEM_HEAP a pool's slot is the system heap's, so AddressSanitizer
reports the pool's wrong free, and a touch of a freed block, of an arena's
block after its end, past it or in its padding, each with its report and
its runtime's status 1.

Under AddressSanitizer with QIVEN3_SYSTEM_HEAP, the build holds memory's
code a second time without it, its blocks in the span and poisoned but for
what is handed out: qiven3_free_demo_poisoned, whose wrong frees fail fast
and whose touches of a freed slot, a heap block's header, the arena's space
past its cursor and its padding are each the sanitizer's use-after-poison;
and qiven3_unit_memory_poisoned, memory's cases on it, each of which passes
there.
"""

import signal
import sys
import unittest

from tests import SKIP_CODE
from tests.drive import binaries
from tests.drive.session import build_option

DEMO = "qiven3_free_demo"
POISONED_DEMO = "qiven3_free_demo_poisoned"
CASES = "qiven3_unit_memory"
POISONED_CASES = "qiven3_unit_memory_poisoned"

# How a demo's blocks sit: on the system heap under AddressSanitizer, in the
# span poisoned under it, on the system heap with no tool, in the span with
# no tool.
SYSTEM, POISONED, UNSEEN_HEAP, PLAIN = "system", "poisoned", "unseen heap", "plain"

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
# Each touch case, and its report with the blocks on the system heap and
# with them poisoned in the span; None where the case skips.
TOUCH = {
    "asan.pool_use_after_free": (b"heap-use-after-free", b"use-after-poison"),
    "asan.heap_use_after_free": (b"heap-use-after-free", b"use-after-poison"),
    "asan.heap_header": (None, b"use-after-poison"),
    "asan.arena_use_after_end": (b"heap-use-after-free", None),
    "asan.arena_past_block": (b"heap-buffer-overflow", b"use-after-poison"),
    "asan.arena_padding": (b"heap-buffer-overflow", b"use-after-poison"),
}


def option_on(name):
    value = build_option(name)
    return value is not None and value.upper() in ("ON", "1", "TRUE", "YES", "Y")


def address():
    return "address" in (build_option("QIVEN3_SANITIZE") or "")


def demos():
    """Each demo the build holds, with how its blocks sit."""
    system_heap = option_on("QIVEN3_SYSTEM_HEAP")
    if not address():
        return [(DEMO, UNSEEN_HEAP if system_heap else PLAIN)]
    if system_heap:
        return [(DEMO, SYSTEM), (POISONED_DEMO, POISONED)]
    return [(DEMO, POISONED)]


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
        for demo, _ in demos():
            for name in HEAP:
                with self.subTest(demo=demo, case=name):
                    self.assert_fails_fast(binaries.run(demo, ["--run", name]))

    def test_pool(self):
        for demo, sit in demos():
            if sit == UNSEEN_HEAP:
                self.skipTest("not applicable: the system heap's wrong free is seen by no tool here")
            for name, (report, out) in POOL.items():
                with self.subTest(demo=demo, case=name):
                    ran = binaries.run(demo, ["--run", name])
                    if sit == SYSTEM:
                        self.assert_reported(ran, report, out)
                    else:
                        self.assert_fails_fast(ran)

    def test_touch(self):
        for demo, sit in demos():
            if sit == UNSEEN_HEAP:
                self.skipTest("not applicable: a touch of the system heap's freed block is seen by no "
                              "tool here")
            for name, (system, poisoned) in TOUCH.items():
                with self.subTest(demo=demo, case=name):
                    report = {SYSTEM: system, POISONED: poisoned}.get(sit)
                    ran = binaries.run(demo, ["--run", name])
                    if report is None:
                        self.assertEqual(ran.status, SKIP_CODE)
                    else:
                        self.assert_reported(ran, report)

    def test_list(self):
        for demo, _ in demos():
            with self.subTest(demo=demo):
                ran = binaries.run(demo, ["--list"])
                self.assertEqual(ran.status, 0)
                self.assertEqual(ran.out.decode("utf-8").splitlines(), sorted([*POOL, *HEAP, *TOUCH]))

    def test_poisoned_cases(self):
        # memory's cases, the free in every position and the fault runs
        # among them, on its code with the blocks in the span, poisoned.
        if not (address() and option_on("QIVEN3_SYSTEM_HEAP")):
            self.skipTest("not applicable: memory's cases are built a second time only under "
                          "AddressSanitizer with QIVEN3_SYSTEM_HEAP")
        listed = binaries.run(POISONED_CASES, ["--list"])
        self.assertEqual(listed.status, 0)
        self.assertEqual(listed.out, binaries.run(CASES, ["--list"]).out)
        names = [line.split("\t")[0] for line in listed.out.decode("utf-8").splitlines()]
        self.assertGreater(len(names), 0)
        passing = (0,) if option_on("QIVEN3_FAULTS") else (0, SKIP_CODE)
        for name in names:
            with self.subTest(case=name):
                ran = binaries.run(POISONED_CASES, ["--run", name])
                self.assertIn(ran.status, passing, ran.out.decode("utf-8", "replace")
                              + ran.err.decode("utf-8", "replace"))


if __name__ == "__main__":
    unittest.main()
