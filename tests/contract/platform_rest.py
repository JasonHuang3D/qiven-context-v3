"""The platform's cases P19 to P26, each defined once over an adapter
(contract/platform_cases.py names the binding's part).

Here: P23 and P24 (PlatformRest), and P26 (PlatformRestMachine, apart, as
it needs machine). P19 and P25 are the files' (contract/platform_files.py);
P20 is the processes' (contract/platform_process.py); P21 and P22 come
with the machine's facts.
"""

import json
import time
from datetime import datetime, timedelta, timezone

from tests.drive.session import once

# The fake's blocks: seed 1's first two, seed 2's first.
SEED_1 = ("783825822a6f9e62da2190e828e4c9d2576e5977e3a0b3620b092dfb9e9996fa",
          "532deabf88729cb43995ab5a9cd49bf9b90a079904dc0645ecda9e47ce7345a9")
SEED_2 = "1309ac3f4e41512820fbf259ae492bb686480eb4a7f5fa4bbc38215266ad984c"

CLOCK_MS = 1_792_229_400_123
CLOCK_AT = "2026-10-17T09:30:00.123Z"

# P26: the text, and the bytes a pipe gets in this machine's code page.
CODE_PAGE_TEXT = "aë中¢€\U0001f600\n"
CODE_PAGE_BYTES = bytes.fromhex("613fd6d03f80aaa13f0d0a")


def printed(ms):
    """The text's printed form of a time, by Python's datetime."""
    t = datetime(1970, 1, 1, tzinfo=timezone.utc) + timedelta(milliseconds=ms)
    return t.strftime("%Y-%m-%dT%H:%M:%S") + f".{ms % 1000:03d}Z"


class PlatformRest:
    def test_P23_random_bytes(self):
        """random_bytes(32) twice: 32 bytes each, the two differing; the
        fake seeded 1, then 2: its blocks."""
        with self.drive() as d:
            first, second = d.random_bytes(32), d.random_bytes(32)
        self.assertEqual((len(first), len(second)), (32, 32))
        self.assertNotEqual(first, second)
        if self.ADAPTER != "fake":
            return
        with self.drive(seed=1) as d:
            self.assertEqual([d.random_bytes(32).hex() for _ in SEED_1], list(SEED_1))
        with self.drive(seed=1) as d:
            # A call takes the next n bytes, across the blocks.
            drawn = d.random_bytes(16) + d.random_bytes(40) + d.random_bytes(8)
            self.assertEqual(drawn.hex(), "".join(SEED_1))
        with self.drive(seed=2) as d:
            self.assertEqual(d.random_bytes(32).hex(), SEED_2)

    def test_P24_clock(self):
        """now() on the fake set to 1,792,229,400,123 ms; the adapter's beside
        the system's clock, within 1 s."""
        self.assertEqual(printed(CLOCK_MS), CLOCK_AT)
        with self.drive() as d:
            if self.ADAPTER == "fake":
                for us in (CLOCK_MS * 1000, CLOCK_MS * 1000 + 999):
                    d.call("set_clock", us=us)
                    self.assertEqual(d.now(), {"ms": CLOCK_MS, "at": CLOCK_AT})
                return
            before = time.time_ns() // 1_000_000
            now = d.now()
            after = time.time_ns() // 1_000_000
        self.assertGreaterEqual(now["ms"], before - 1000)
        self.assertLessEqual(now["ms"], after + 1000)
        self.assertEqual(now["at"], printed(now["ms"]))


class PlatformRestMachine:
    def test_P26_code_page(self):
        """stdout() to a pipe: the code page's bytes, '?' for a character it
        lacks, CRLF for LF."""
        self.needs("machine")
        ran = once(self.drive_args(), {"op": "stdout", "text": CODE_PAGE_TEXT}, drive=self.DRIVE)
        self.assertEqual(ran.status, 0, ran.err)
        self.assertEqual(ran.out[:len(CODE_PAGE_BYTES)], CODE_PAGE_BYTES)
        reply = ran.out[len(CODE_PAGE_BYTES):]
        self.assertEqual(len(reply.splitlines()), 1, reply)
        self.assertTrue(json.loads(reply)["ok"], reply)
