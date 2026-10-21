"""K14: no secret and no cognition text reaches a log line or a report's
events.

The drives run each operation with a request holding a sentinel secret and
a cognition sentinel, both built when the test runs, in its texts and, as
hexadecimal, in its bytes; no line of the log's file (QIVEN3_LOG) holds
either, as text or as hexadecimal. A run the HAL fake ends by a caller's
mistake (H8) writes the entry's report with the last events, which hold
neither. Each request is an operation of the log: its op.end is there, and
in a build whose minimum is debug or below, its drive.request names it.

The controls: a copy of a log file with the secret written in is found, as
is a copy of a report's events with the cognition text.

Each package adds its operations to requests() below.
"""

import json
import secrets
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive import session

CPU = {"id": "cpu0", "back_end": "cpu", "name": "the CPU", "memory": 1 << 20}


def sentinels():
    """The secret, in a known token's form, and the cognition text, built
    here: no file holds either."""
    secret = "sk-" + "proj-" + secrets.token_hex(24)
    cognition = "cognition sentinel " + secrets.token_hex(8)
    return secret, cognition


def forms(*texts):
    """Each text as UTF-8 bytes, and as hexadecimal, either case."""
    out = []
    for text in texts:
        raw = text.encode("utf-8")
        out += [raw, raw.hex().encode(), raw.hex().upper().encode()]
    return out


def found(lines, texts):
    """The lines holding any form of texts."""
    marks = forms(*texts)
    return [line for line in lines if any(mark in line for mark in marks)]


def requests(secret, cognition, place, windows=False, hal=False):
    """A request for each operation of a drive, each holding both sentinels
    where it takes a text or bytes; the Windows drive's paths under place,
    and no stdout, whose text would come before the reply."""
    path = str(place / (secret + "-" + cognition.replace(" ", "-")))
    data = (secret + " " + cognition).encode("utf-8").hex()
    both = secret + " " + cognition
    out = [
        {"op": "adapter_for", "system": both, "config": path},
        {"op": "current", "config": path},
        {"op": "name"},
        {"op": "config_dir", "create": False},
        {"op": "resolve", "path": path},
        {"op": "normcase", "path": path},
        {"op": "facts", "path": path},
        {"op": "synced", "path": path},
        {"op": "replace", "path": path, "data": data},
        {"op": "create", "path": path + "-dir", "directory": True},
        {"op": "read", "path": path},
        {"op": "program", "name": both},
        {"op": "run", "argv": [path, both], "cwd": path, "env": [[secret, cognition]], "data": data},
        {"op": "code_page"},
        {"op": "residual", "paths": [path]},
        {"op": "random_bytes", "n": 4},
        {"op": "now"},
        {"op": "pages", "n": 1},
        {"op": "pages_check", "id": 1},
        {"op": "pages_free", "id": 1},
        {"op": "read", "path": path, "budget": 1},
        {"op": both},
    ]
    if not windows:
        out += [
            {"op": "stdout", "text": both},
            {"op": "set_clock", "us": 1},
            {"op": "set_read_error", "path": path, "detail": both},
            {"op": "read", "path": path},
            {"op": "refuse_pages", "at": 1},
            {"op": "pages", "n": 1},
            {"op": "calls"},
            {"op": "written"},
            {"op": "lent"},
        ]
    if hal:
        out += [
            {"op": "hal_fake", "devices": [CPU]},
            {"op": "hal_facts"},
            {"op": "hal_device", "device": both, "capability": both},
            {"op": "hal_allocate", "device": CPU, "size": 8},
            {"op": "hal_upload", "buffer": 1, "data": data[:16]},
            {"op": "hal_download", "buffer": 1},
            {"op": "hal_run", "kernel": both, "buffers": [1]},
            {"op": "hal_free", "buffer": 1},
            {"op": "hal_counts"},
        ]
    return out


# H8's caller's mistake: an upload into a buffer the other fake gave ends
# the drive by the fail-fast, its report on standard error.
MISTAKE = ({"op": "hal_fake", "devices": [CPU]},
           {"op": "hal_allocate", "device": CPU, "size": 8, "other": True},
           {"op": "hal_upload", "buffer": 1, "data": "00" * 8})


def minimum():
    """The build's minimum level."""
    return session.build_option("QIVEN3_LOG_LEVEL") or "info"


def report_events(err):
    """The report's event lines, by its events line; none without one."""
    lines = err.splitlines()
    for i, line in enumerate(lines):
        if line.startswith(b"fatal: events "):
            return lines[i + 1:i + 1 + int(line.split()[2])]
    return []


class K14(unittest.TestCase):
    ctest_labels = ("fake",)

    def setUp(self):
        self.place = Path(tempfile.mkdtemp(prefix="qiven3-k14-"))
        self.secret, self.cognition = sentinels()

    def tearDown(self):
        shutil.rmtree(self.place, ignore_errors=True)

    def drive(self, name, args, sent, log):
        """The drive run on sent with its log in log: its run."""
        env = session.environment(QIVEN3_LOG=str(log))
        return session.once(args, *sent, drive=name, env=env)

    def check_log(self, log, sent):
        """The log's lines: JSON, neither sentinel, an op.end a request, and,
        at debug, each drive.request named by its op."""
        lines = log.read_bytes().splitlines()
        self.assertEqual(found(lines, (self.secret, self.cognition)), [])
        events = [json.loads(line) for line in lines]
        ends = [e for e in events if e["event"] == "op.end"]
        self.assertEqual(len(ends), len(sent), [e["name"] for e in ends])
        self.assertTrue(any(e["event"] == "error.typed" for e in events))
        if minimum() in ("trace", "debug"):
            names = [e["name"] for e in events if e["event"] == "drive.request"]
            ops = [r["op"] for r in sent]
            self.assertEqual(names, [op if op[:3] != "sk-" else "unknown" for op in ops])
        return lines

    def test_K14_fake(self):
        hal = session.has_hal()
        root = self.place / "root"
        root.mkdir()
        sent = requests(self.secret, self.cognition, root, hal=hal)
        log = self.place / "fake.jsonl"
        args = ["--root", root] + (["--hal", "fake"] if hal else [])
        ran = self.drive(session.FAKE, args, sent, log)
        self.assertEqual(ran.status, 0, ran)
        lines = self.check_log(log, sent)
        # The control: the secret written into a copy is found.
        self.assertEqual(found(lines + [b'{"x":"' + self.secret.encode() + b'"}'], (self.secret,)),
                         [b'{"x":"' + self.secret.encode() + b'"}'])

    @unittest.skipUnless(sys.platform == "win32", "not applicable: the Windows drive is Windows's alone")
    def test_K14_windows(self):
        hal = session.has_hal()
        sent = requests(self.secret, self.cognition, self.place, windows=True, hal=hal)
        log = self.place / "windows.jsonl"
        ran = self.drive(session.WINDOWS, ["--hal", "fake"] if hal else [], sent, log)
        self.assertEqual(ran.status, 0, ran)
        self.check_log(log, sent)

    def test_K14_report(self):
        if not session.has_hal():
            self.skipTest("not applicable: the HAL is off, so no caller's mistake ends a drive")
        if session.build_option("QIVEN3_SANITIZE"):
            self.skipTest("not applicable: under a sanitizer its report stands for the entry's")
        root = self.place / "root"
        root.mkdir()
        # The HAL's own requests left out, so that the mistake's buffer is 1.
        sent = requests(self.secret, self.cognition, root)
        log = self.place / "report.jsonl"
        ran = self.drive(session.FAKE, ["--root", root, "--hal", "fake"], sent + list(MISTAKE), log)
        self.assertNotEqual(ran.status, 0, ran)
        events = report_events(ran.err)
        self.assertGreater(len(events), 0, ran.err[:400])
        self.assertEqual(found(events, (self.secret, self.cognition)), [])
        self.assertEqual(found(log.read_bytes().splitlines(), (self.secret, self.cognition)), [])
        # The control: the cognition text written into a copy is found.
        copy = events + [b'{"y":"' + self.cognition.encode() + b'"}']
        self.assertEqual(len(found(copy, (self.cognition,))), 1)


if __name__ == "__main__":
    unittest.main()
